#include "asset_manager.h"

#include <chrono>

#include "static_model.h"
#include "filesystem.h"
#include "log.h"
#include "texture.h"
#include "formatters_glm.h"

#include <assimp/postprocess.h>
#include <assimp/material.h>


using namespace std::chrono;
using namespace std::literals;

namespace RGL
{

static inline glm::vec3 vec3_cast(const aiVector3D& v)   { return glm::vec3(v.x, v.y, v.z); }


std::shared_ptr<const StaticModel> AssetManager::staticMesh(std::string_view name)
{
	static constexpr auto models_prefix = "/models/"sv;

	// cut anything up to (and including) "/textures/"
	if(auto pmod = name.find(models_prefix); pmod != std::string_view::npos)
		name = name.substr(pmod + models_prefix.size());

	if(auto found = _static_meshes.find(name); found != _static_meshes.end())
		return found->second.lock();

		   // TODO: pool?
	auto *mesh = new StaticModel();

	Log::info("Loading mesh: {}", name);
	loadStaticMesh(*mesh, FileSystem::getResourcesPath() / "models" / name); // TODO: use 'models_prefix'
	assert(*mesh);

	auto mesh_ref = std::shared_ptr<const StaticModel>(mesh, [this, name=std::string(name)](auto *) {
		delete_static_mesh(name);
	});

	_static_meshes[std::string(name)] = mesh_ref;

	return mesh_ref;
}

void AssetManager::delete_static_mesh(std::string_view name)
{
	auto found = _static_meshes.find(name);
	if(found != _static_meshes.end())
	{
		Log::info("Freeing mesh: {}", name);
		_static_meshes.erase(found);
	}
}

bool AssetManager::loadStaticMesh(StaticModel &model, const std::filesystem::path& filepath)
{
	Assimp::Importer importer;
	// TODO: importer.SetIOHandler(compressionLayer);
	const auto *scene = importer.ReadFile(filepath.generic_string(),
										  aiProcess_Triangulate              |
											  aiProcess_GenSmoothNormals         |
											  aiProcess_GenUVCoords              |
											  aiProcess_CalcTangentSpace         |
											  aiProcess_FlipUVs                  |
											  aiProcess_JoinIdenticalVertices    |
											  aiProcess_RemoveRedundantMaterials |
											  aiProcess_GenBoundingBoxes );

	model._ok = scene and scene->mFlags != AI_SCENE_FLAGS_INCOMPLETE and scene->mRootNode;

	if(not model._ok)
	{
		Log::error("loading mesh failed: {}: {}", filepath.generic_string().c_str(), importer.GetErrorString());
		return false;
	}

	model._ok = parseStaticScene(model, scene, filepath);
	return model._ok;
}

bool AssetManager::parseStaticScene(StaticModel &model, const aiScene *scene, const std::filesystem::path& filepath)
{
	const auto T0 = steady_clock::now();

	const auto filename = filepath.filename();

	model.m_mesh_parts.resize(scene->mNumMeshes);
	model.m_materials.resize(scene->mNumMaterials);

	VertexData vertex_data;

	uint32_t vertices_count = 0;
	uint32_t indices_count  = 0;

	/* Count the number of vertices and indices. */
	for (uint32_t idx = 0; idx < model.m_mesh_parts.size(); ++idx)
	{
		model.m_mesh_parts[idx].m_material_index = scene->mNumMaterials > 0 ? scene->mMeshes[idx]->mMaterialIndex : INVALID_MATERIAL;
		model.m_mesh_parts[idx].m_indices_count  = scene->mMeshes[idx]->mNumFaces * 3;
		model.m_mesh_parts[idx].m_base_vertex    = vertices_count;
		model.m_mesh_parts[idx].m_base_index     = indices_count;

		vertices_count += scene->mMeshes[idx]->mNumVertices;
		indices_count  += model.m_mesh_parts[idx].m_indices_count;
	}

	if(scene->mNumCameras > 0)
	{
		model._cameras.reserve(scene->mNumCameras);
		for(auto idx = 0u; idx < scene->mNumCameras; ++idx)
		{
			auto *camera = scene->mCameras[idx];
			assert(camera);
			// TODO
			Log::warning("StaticModel::ParseScene() Cameras not implemented");
		}
	}

		   // Reserve space for the vertex attributes and indices
	vertex_data.positions.reserve(vertices_count);
	vertex_data.texcoords.reserve(vertices_count);
	vertex_data.normals.reserve(vertices_count);
	vertex_data.tangents.reserve(vertices_count);
	vertex_data.indices.reserve(indices_count);

	// Load each mesh parts
	for (uint32_t idx = 0; idx < model.m_mesh_parts.size(); ++idx)
	{
		auto *mesh = scene->mMeshes[idx];
		loadMeshPart(model, mesh, vertex_data);

		const auto box_min = vec3_cast(mesh->mAABB.mMin);
		const auto box_max = vec3_cast(mesh->mAABB.mMax);

		model._aabb.expand(box_min);
		model._aabb.expand(box_max);

		Log::info("[{}] added sub-mesh {}: {} vertices  AABB: {:.1f}  ->  {:.1f}  ({:.1f}x{:.1f}x{:.1f})",
				  filename.string(),
				  idx,
				  mesh->mNumVertices,
				  box_min, box_max,
				  box_max.x - box_min.x, box_max.y - box_min.y, box_max.z - box_min.z
		);
	}

	if(not loadMaterials(model, scene, filepath))
	{
		Log::error("loading mesh failed: {}: Could not load the materials", filepath.generic_string());
		return false;
	}

	// Populate buffers on the GPU with the model's data
	// if mesh loading runs in the background, _this_ function needs to be run on the main thread
	createStaticBuffers(model, vertex_data);

	const auto T1 = steady_clock::now();

	Log::info("Loaded mesh {}  ({:.1f} x {:.1f} x {:.1f})  ({})", filepath.string().c_str(), model._aabb.width(), model._aabb.height(), model._aabb.depth(), duration_cast<milliseconds>(T1 - T0));

	return true;
}

void AssetManager::loadMeshPart(StaticModel &model, const aiMesh* mesh, VertexData& vertex_data)
{
	const glm::vec3 zero_vec3 = glm::zero<glm::vec3>();

	for (uint32_t idx = 0; idx < mesh->mNumVertices; ++idx)
	{
		auto pos      = vec3_cast(mesh->mVertices[idx]);
		auto texcoord = mesh->HasTextureCoords(0)        ? vec3_cast(mesh->mTextureCoords[0][idx]) : zero_vec3;
		auto normal   = mesh->HasNormals()               ? vec3_cast(mesh->mNormals[idx])          : zero_vec3;
		auto tangent  = mesh->HasTangentsAndBitangents() ? vec3_cast(mesh->mTangents[idx])         : zero_vec3;

		vertex_data.positions.push_back(pos);
		model._sphere.expand(pos);
		vertex_data.texcoords.push_back(glm::vec2(texcoord.x, texcoord.y));
		vertex_data.normals.push_back(normal);
		vertex_data.tangents.push_back(tangent);
	}

	for (uint32_t idx = 0; idx < mesh->mNumFaces; ++idx)
	{
		const aiFace& face = mesh->mFaces[idx];
		assert(face.mNumIndices == 3);

		for (auto idx = 0u; idx < face.mNumIndices; ++idx)
			vertex_data.indices.push_back(face.mIndices[idx]);
	}
}

bool AssetManager::loadMaterials(StaticModel &model, const aiScene* scene, const std::filesystem::path& filepath)
{
	// Extract the directory part from the file name
	// auto last_slash_index = filepath.generic_string().rfind("/");
	// fs::path dir;

	// if (last_slash_index == std::string::npos)
	// 	dir = ".";
	// else if (last_slash_index == 0)
	// 	dir = "/";
	// else
	// 	dir = filepath.generic_string().substr(0, last_slash_index);

	bool ret = true;

	for (uint32_t idx = 0; idx < scene->mNumMaterials; ++idx)
	{
		auto *material = scene->mMaterials[idx];

		auto &mesh_material = model.m_materials[idx];
		ret |= loadMaterialTextures(mesh_material, scene, material, aiTextureType_BASE_COLOR,        Material::TextureType::ALBEDO);
		ret |= loadMaterialTextures(mesh_material, scene, material, aiTextureType_NORMALS,           Material::TextureType::NORMAL);
		ret |= loadMaterialTextures(mesh_material, scene, material, aiTextureType_EMISSIVE,          Material::TextureType::EMISSIVE);
		ret |= loadMaterialTextures(mesh_material, scene, material, aiTextureType_AMBIENT_OCCLUSION, Material::TextureType::AO);
		ret |= loadMaterialTextures(mesh_material, scene, material, aiTextureType_DIFFUSE_ROUGHNESS, Material::TextureType::ROUGHNESS);
		ret |= loadMaterialTextures(mesh_material, scene, material, aiTextureType_METALNESS,         Material::TextureType::METALLIC);

		/* Load material parameters */
		aiColor3D color_rgb;
		aiColor4D color_rgba;
		float value;

		if (AI_SUCCESS == material->Get(AI_MATKEY_BASE_COLOR, color_rgba))
			model.m_materials[idx].set("u_albedo"sv, glm::vec3(color_rgba.r, color_rgba.g, color_rgba.b));
		if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_EMISSIVE, color_rgb))
			model.m_materials[idx].set("u_emission"sv, glm::vec3(color_rgb.r, color_rgb.g, color_rgb.b));
		if (AI_SUCCESS == material->Get(AI_MATKEY_EMISSIVE_INTENSITY, value))
			model.m_materials[idx].set("u_emission_strength"sv, value);
		if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_AMBIENT, color_rgb))
			model.m_materials[idx].set("u_ao"sv, (color_rgb.r + color_rgb.g + color_rgb.b) / 3.0f);
		if (AI_SUCCESS == material->Get(AI_MATKEY_ROUGHNESS_FACTOR, value))
			model.m_materials[idx].set("u_roughness"sv, value);
		if (AI_SUCCESS == material->Get(AI_MATKEY_METALLIC_FACTOR, value))
			model.m_materials[idx].set("u_metallic"sv, value);
	}

	return ret;
}

bool AssetManager::loadMaterialTextures(Material &mesh_material, const aiScene* scene, const aiMaterial* material, aiTextureType type, Material::TextureType texture_type)
{
	if (material->GetTextureCount(type) > 0)
	{
		aiString path;
		aiTextureMapMode texture_map_mode[3];

		// Only one texture of a given type is being loaded
		if (material->GetTexture(type, 0, &path, NULL, NULL, NULL, NULL, texture_map_mode) == AI_SUCCESS)
		{
			const bool is_srgb = (type == aiTextureType_DIFFUSE) or (type == aiTextureType_EMISSIVE) or (type == aiTextureType_BASE_COLOR);

			std::shared_ptr<Texture2D> texture = std::make_shared<Texture2D>();
			const aiTexture* paiTexture = scene->GetEmbeddedTexture(path.C_Str());

			if (paiTexture)
			{
				// Load embedded
				uint32_t data_size = paiTexture->mHeight > 0 ? paiTexture->mWidth * paiTexture->mHeight : paiTexture->mWidth;

				if (texture->Load(reinterpret_cast<unsigned char*>(paiTexture->pcData), data_size, is_srgb))
				{
					Log::debug("Loaded embedded texture for the model {}", path.C_Str());
					mesh_material.set(texture_type, texture);

					if (texture_map_mode[0] == aiTextureMapMode_Wrap)
					{
						texture->SetWrapping(RGL::TextureWrappingAxis::U, RGL::TextureWrappingParam::Repeat);
						texture->SetWrapping(RGL::TextureWrappingAxis::V, RGL::TextureWrappingParam::Repeat);
					}
				}
				else
				{
					Log::error("\x1b[97;41;1mError\x1b[m Loading embedded texture for the model failed: {}", path.C_Str());
					return false;
				}
			}
			else
			{
				const auto T0 = steady_clock::now();

				// Load from file
				fs::path full_path { path.data };

				auto texture = this->texture(full_path.native(), is_srgb);
				// if (!texture->Load(full_path, is_srgb))
				// {
				// 	Log::error("\x1b[97;41;1mError\x1b[m Loading texture failed {}.", full_path);
				// 	return false;
				// }
				// else
				// {
				const auto T1 = steady_clock::now();
				mesh_material.set(texture_type, texture);

				// if (texture_map_mode[0] == aiTextureMapMode_Wrap)
				// {
				// 	texture->SetWrapping(RGL::TextureWrappingAxis::U, RGL::TextureWrappingParam::Repeat);
				// 	texture->SetWrapping(RGL::TextureWrappingAxis::V, RGL::TextureWrappingParam::Repeat);
				// }
				// }
			}
		}

		static const dense_map<Material::TextureType, std::string_view> s_texture_flag_uniform_name {
			{ Material::TextureType::ALBEDO,    "u_has_albedo_map"sv },
			{ Material::TextureType::NORMAL,    "u_has_normal_map"sv },
			{ Material::TextureType::EMISSIVE,  "u_has_emissive_map"sv },
			{ Material::TextureType::AO,        "u_has_ao_map"sv },
			{ Material::TextureType::METALLIC,  "u_has_metallic_map"sv },
			{ Material::TextureType::ROUGHNESS, "u_has_roughness_map"sv }
		};

		auto uniform_found = s_texture_flag_uniform_name.find(texture_type);
		if(uniform_found != s_texture_flag_uniform_name.end())
			mesh_material.set(uniform_found->second, true);
	}

	return true;
}

void AssetManager::createStaticBuffers(StaticModel &model, VertexData& vertex_data)
{
	bool has_tangents = !vertex_data.tangents.empty();

	const GLsizei positions_size_bytes = GLsizei(vertex_data.positions.size() * sizeof(vertex_data.positions[0]));
	const GLsizei texcoords_size_bytes = GLsizei(vertex_data.texcoords.size() * sizeof(vertex_data.texcoords[0]));
	const GLsizei normals_size_bytes   = GLsizei(vertex_data.normals  .size() * sizeof(vertex_data.normals  [0]));
	const GLsizei tangents_size_bytes  = GLsizei(has_tangents ? vertex_data.tangents .size() * sizeof(vertex_data.tangents [0]) : 0);
	const GLsizei total_size_bytes     = positions_size_bytes + texcoords_size_bytes + normals_size_bytes + tangents_size_bytes;

	glCreateBuffers     (1, &model.m_vbo_name);
	glNamedBufferStorage(model.m_vbo_name, total_size_bytes, nullptr, GL_DYNAMIC_STORAGE_BIT);

	GLintptr offset = 0;
	glNamedBufferSubData(model.m_vbo_name, offset, positions_size_bytes, vertex_data.positions.data());

	offset += positions_size_bytes;
	glNamedBufferSubData(model.m_vbo_name, offset, texcoords_size_bytes, vertex_data.texcoords.data());

	offset += texcoords_size_bytes;
	glNamedBufferSubData(model.m_vbo_name, offset, normals_size_bytes, vertex_data.normals.data());

	if(has_tangents)
	{
		offset += normals_size_bytes;
		glNamedBufferSubData(model.m_vbo_name, offset, tangents_size_bytes, vertex_data.tangents.data());
	}

	glCreateBuffers     (1, &model.m_ibo_name);
	glNamedBufferStorage(model.m_ibo_name, GLsizeiptr(sizeof(vertex_data.indices[0]) * vertex_data.indices.size()), vertex_data.indices.data(), GL_DYNAMIC_STORAGE_BIT);

	glCreateVertexArrays(1, &model.m_vao_name);

	offset = 0;
	glVertexArrayVertexBuffer(model.m_vao_name, 0 /* bindingindex*/, model.m_vbo_name, offset, sizeof(vertex_data.positions[0]) /*stride*/);

	offset += positions_size_bytes;
	glVertexArrayVertexBuffer(model.m_vao_name, 1 /* bindingindex*/, model.m_vbo_name, offset, sizeof(vertex_data.texcoords[0]) /*stride*/);

	offset += texcoords_size_bytes;
	glVertexArrayVertexBuffer(model.m_vao_name, 2 /* bindingindex*/, model.m_vbo_name,  offset, sizeof(vertex_data.normals[0]) /*stride*/);

	if (has_tangents)
	{
		offset += normals_size_bytes;
		glVertexArrayVertexBuffer(model.m_vao_name, 3 /* bindingindex*/, model.m_vbo_name, offset, sizeof(vertex_data.tangents[0]) /*stride*/);
	}

	glVertexArrayElementBuffer(model.m_vao_name,model. m_ibo_name);

	glEnableVertexArrayAttrib(model.m_vao_name, 0 /*attribindex*/); // positions
	glEnableVertexArrayAttrib(model.m_vao_name, 1 /*attribindex*/); // texcoords
	glEnableVertexArrayAttrib(model.m_vao_name, 2 /*attribindex*/); // normals
	if (has_tangents) glEnableVertexArrayAttrib(model.m_vao_name, 3 /*attribindex*/); // tangents

	glVertexArrayAttribFormat(model.m_vao_name, 0 /*attribindex */, 3 /* size */, GL_FLOAT, GL_FALSE, 0 /*relativeoffset*/);
	glVertexArrayAttribFormat(model.m_vao_name, 1 /*attribindex */, 2 /* size */, GL_FLOAT, GL_FALSE, 0 /*relativeoffset*/);
	glVertexArrayAttribFormat(model.m_vao_name, 2 /*attribindex */, 3 /* size */, GL_FLOAT, GL_FALSE, 0 /*relativeoffset*/);
	if (has_tangents) glVertexArrayAttribFormat(model.m_vao_name, 3 /*attribindex */, 3 /* size */, GL_FLOAT, GL_FALSE, 0 /*relativeoffset*/);

	glVertexArrayAttribBinding(model.m_vao_name, 0 /*attribindex*/, 0 /*bindingindex*/); // positions
	glVertexArrayAttribBinding(model.m_vao_name, 1 /*attribindex*/, 1 /*bindingindex*/); // texcoords
	glVertexArrayAttribBinding(model.m_vao_name, 2 /*attribindex*/, 2 /*bindingindex*/); // normals
	if (has_tangents) glVertexArrayAttribBinding(model.m_vao_name, 3 /*attribindex*/, 3 /*bindingindex*/); // tangents
}


} // RGL