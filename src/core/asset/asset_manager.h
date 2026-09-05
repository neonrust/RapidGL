#pragma once

#include <memory>
#include <filesystem>

#include "container_types.h"
#include "material.h"

#include <assimp/material.h> // aiTextureType, Material::TextureType

struct aiScene;
struct aiMesh;
struct aiMaterial;

namespace RGL
{

class StaticModel;
class Texture2D;
struct VertexData;

class AssetManager
{
public:
	static AssetManager &the();
	// must be called before OpenGL shut down
	static void shut_down();

	std::pair<std::shared_ptr<const StaticModel>, MaterialCSet> staticMesh(std::string_view name);
	std::shared_ptr<const Texture2D>   texture(std::string_view name, bool is_srgb=false);

private:
	void delete_static_mesh(std::string_view name, StaticModel *mesh);
	void delete_texture2d(std::string_view name, Texture2D *tex);

	// TODO: move these to a "mesh loader" thingy
	bool loadStaticMesh(StaticModel &model, const std::filesystem::path &filepath, MaterialSet &materials);
	bool parseStaticScene(StaticModel &model, const aiScene *scene, const std::filesystem::path &filepath, MaterialSet &materials);
	void loadMeshPart(StaticModel &model, const aiMesh *mesh, VertexData &vertex_data);
	bool loadMaterials(MaterialSet &materials, const aiScene *scene, const std::filesystem::path &filepath);
	void createStaticBuffers(StaticModel &model, VertexData &vertex_data);
	bool loadMaterialTextures(Material &mesh_material, const aiScene *scene, const aiMaterial *material, aiTextureType type, Material::TextureType texture_type);

private:
	AssetManager();

	// only weak ptrs; does not hold  references
	string_map<std::weak_ptr<const StaticModel>> _static_meshes;
	string_map<MaterialCSet> _static_mesh_default_materials;
	string_map<std::weak_ptr<const Texture2D>> _textures;
};

} // RGL