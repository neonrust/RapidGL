#pragma once

#include <filesystem>
#include <memory>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#include "bounds.h"
#include "mesh_part.h"
#include "material.h"
#include "shader.h"

namespace RGL
{

struct VertexData
{
	std::vector<glm::vec3> positions;
	std::vector<glm::vec2> texcoords;
	std::vector<glm::vec3> normals;
	std::vector<glm::vec3> tangents;
	std::vector<uint32_t>  indices;
};

enum class DrawMode {
	POINTS         = GL_POINTS,
	LINES          = GL_LINES,
	TRIANGLES      = GL_TRIANGLES,
	TRIANGLE_STRIP = GL_TRIANGLE_STRIP,
	PATCHES        = GL_PATCHES
};

class StaticModel
{
public:
	StaticModel()
		: m_unit_scale(1),
		m_vao_name  (0),
		m_vbo_name  (0),
		m_ibo_name  (0),
		m_draw_mode (DrawMode::TRIANGLES)
	{
	}

	virtual ~StaticModel() { Release(); }

	StaticModel           (const StaticModel&) = delete;
	StaticModel& operator=(const StaticModel&) = delete;

	StaticModel(StaticModel&& other) noexcept
		: m_mesh_parts(std::move(other.m_mesh_parts)),
		m_materials  (std::move(other.m_materials)),
		m_unit_scale(other.m_unit_scale),
		m_vao_name  (other.m_vao_name),
		m_vbo_name  (other.m_vbo_name),
		m_ibo_name  (other.m_ibo_name),
		m_draw_mode (other.m_draw_mode)
	{
		other.m_unit_scale = 1;
		other.m_vao_name   = 0;
		other.m_vbo_name   = 0;
		other.m_ibo_name   = 0;
		other.m_draw_mode  = DrawMode::TRIANGLES;
	}

	StaticModel& operator=(StaticModel&& other) noexcept
	{
		if (this != &other)
		{
			Release();

			std::swap(m_mesh_parts, other.m_mesh_parts);
			std::swap(m_materials,  other.m_materials);
			std::swap(m_unit_scale, other.m_unit_scale);
			std::swap(m_vao_name,   other.m_vao_name);
			std::swap(m_vbo_name,   other.m_vbo_name);
			std::swap(m_ibo_name,   other.m_ibo_name);
			std::swap(m_draw_mode,  other.m_draw_mode);
		}

		return *this;
	}

	virtual void AddAttributeBuffer(GLuint attrib_index, GLuint binding_index, GLint format_size, GLenum data_type, GLuint buffer_id, GLsizei stride, GLuint divisor = 0);
	virtual void AddTexture(const std::shared_ptr<Texture2D> & texture, Material::TextureType texture_type = Material::TextureType::ALBEDO, uint32_t mesh_id = 0);

	virtual void SetDrawMode(DrawMode mode) { m_draw_mode = mode; }
	virtual float GetUnitScaleFactor() const { return m_unit_scale; }

	// TODO: convert to factory function
	virtual bool Load(const std::filesystem::path& filepath);

	// TODO: move to a Renderer-thingy class
	//   _renderer->submit(mesh);
	virtual void Render(uint32_t num_instances = 0);
	virtual void Render(Shader &shader, uint32_t num_instances = 0);

	// TODO: convert these to a "mesh primitive factory"
	virtual void GenCone       (float    height      = 3.0f, float radius         = 1.5f, uint32_t slices = 10, uint32_t stacks = 10);
	virtual void GenCube       (float    radius      = 1.0f, float texcoord_scale = 1.0f);
	virtual void GenCubeMap    (float    radius      = 1.0f);
	virtual void GenCylinder   (float    height      = 3.0f, float    radius      = 1.5f, uint32_t slices = 10);
	virtual void GenPlane      (float    width       = 1.0f, float    height      = 1.0f, uint32_t slices = 5, uint32_t stacks = 5);
	virtual void GenPlaneGrid  (float    width       = 1.0f, float    height      = 1.0f, uint32_t slices = 5, uint32_t stacks = 5);
	virtual void GenSphere     (float    radius      = 1.5f, uint32_t slices      = 12);
	virtual void GenTorus      (float    innerRadius = 1.0f, float    outerRadius = 2.0f, uint32_t slices = 10, uint32_t stacks = 10);
	virtual void GenTrefoilKnot(uint32_t slices      = 100,  uint32_t stacks      = 20);
	virtual void GenPQTorusKnot(uint32_t slices      = 256,  uint32_t stacks      = 16,   int p = 2, int q = 3, float knot_r = 0.75, float tube_r = 0.15);
	virtual void GenQuad       (float    width       = 1.0f, float    height      = 1.0f);

	inline const bounds::AABB &aabb() const { return _aabb; }

protected:
	// For converting between ASSIMP and glm
	static inline glm::vec3 vec3_cast(const aiVector3D& v)   { return glm::vec3(v.x, v.y, v.z); }
	static inline glm::vec2 vec2_cast(const aiVector3D& v)   { return glm::vec2(v.x, v.y); }
	static inline glm::quat quat_cast(const aiQuaternion& q) { return glm::quat(q.w, q.x, q.y, q.z); }
	static inline glm::mat4 mat4_cast(const aiMatrix4x4& m)  { return glm::transpose(glm::make_mat4(&m.a1)); }
	static inline glm::mat4 mat4_cast(const aiMatrix3x3& m)  { return glm::transpose(glm::make_mat3(&m.a1)); }

	virtual bool ParseScene(const aiScene* scene, const std::filesystem::path& filepath);
	virtual void LoadMeshPart(const aiMesh* mesh, VertexData& vertex_data);
	virtual bool LoadMaterials(const aiScene* scene, const std::filesystem::path& filepath);
	virtual bool LoadMaterialTextures(const aiScene* scene, const aiMaterial* material, uint32_t material_index, aiTextureType type, Material::TextureType texture_type, const std::string& directory);
	virtual void CreateBuffers(VertexData& vertex_data);

	virtual void CalcTangentSpace(VertexData& vertex_data);
	virtual void GenPrimitive(VertexData& vertex_data, bool generate_tangents = true);

	void Release();

	std::vector<MeshPart> m_mesh_parts;
	std::vector<Material> m_materials;

	float    m_unit_scale;
	GLuint   m_vao_name;
	GLuint   m_vbo_name;
	GLuint   m_ibo_name;
	DrawMode m_draw_mode;
	bounds::AABB _aabb;
};

}
