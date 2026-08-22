#pragma once

#include "container_types.h"
#include <glm/vec3.hpp>
#include <string_view>


namespace RGL
{

class Texture2D;

class Material
{
public:
	// Texture type order must match the order in pbr-lighting.glh
	// TextureType is being cast to uint32_t during the mesh rendering.
	enum class TextureType
	{
		ALBEDO,
		NORMAL,
		METALLIC,
		ROUGHNESS,
		AO,
		EMISSIVE
	};

	Material();
	~Material();

	void set(TextureType texture_type, std::string_view name, const std::shared_ptr<const Texture2D> &texture);

	void set(std::string_view uniform_name, const glm::vec3 &vec);
	void set(std::string_view uniform_name, float value);
	void set(std::string_view uniform_name, bool value);

	std::shared_ptr<const Texture2D> getTexture(TextureType texture_type);
	std::string_view textureName(TextureType texture_type) const;
	glm::vec3                  getVector3(const std::string_view& uniform_name);
	float                      getFloat  (const std::string_view& uniform_name);
	bool                       getBool   (const std::string_view& uniform_name);

	size_t hash() const;

private:
	dense_map<TextureType, std::string> _texture_names;
	dense_map<TextureType, std::shared_ptr<const Texture2D>> m_texture_map;
	string_map<glm::vec3> m_vec3_map;
	string_map<float>     m_float_map;
	string_map<bool>      m_bool_map;

	mutable size_t _hash { 0 };
	mutable bool _hash_dirty { true };

	friend class StaticModel;
};

using MaterialCRef = std::shared_ptr<const Material>;
using MaterialCSet = std::vector<MaterialCRef>;

using MaterialRef = std::shared_ptr<Material>;
using MaterialSet = std::vector<MaterialRef>;

} // RGL
