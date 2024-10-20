#include "material.h"

#include <string_view>
using namespace std::literals;

namespace RGL
{
    Material::Material()
    {
		m_vec3_map.reserve(4);
		m_float_map.reserve(4);
		m_bool_map.reserve(8);

		setVector3("u_albedo"sv,            glm::vec3(1.0f));
		setVector3("u_emission"sv,          glm::vec3(0.0f));
		setFloat  ("u_ao"sv,                1.0f);
		setFloat  ("u_roughness"sv,         0.0f);
		setFloat  ("u_metallic"sv,          0.0f);
		setBool   ("u_has_albedo_map"sv,    false);
		setBool   ("u_has_normal_map"sv,    false);
		setBool   ("u_has_emissive_map"sv,  false);
		setBool   ("u_has_ao_map"sv,        false);
		setBool   ("u_has_metallic_map"sv,  false);
		setBool   ("u_has_roughness_map"sv, false);
    }

    Material::~Material()
    {
    }

	void Material::setTexture(TextureType texture_type, const std::shared_ptr<Texture2D>& texture)
    {
        m_texture_map[texture_type] = texture;
    }

	void Material::setVector3(const std::string_view &uniform_name, const glm::vec3& vector3)
    {
        m_vec3_map[uniform_name] = vector3;
    }

	void Material::setFloat(const std::string_view &uniform_name, float value)
    {
        m_float_map[uniform_name] = value;
    }

	void Material::setBool(const std::string_view &uniform_name, bool value)
    {
        m_bool_map[uniform_name] = value;
    }

	std::shared_ptr<Texture2D> Material::getTexture(TextureType texture_type)
    {
		auto found = m_texture_map.find(texture_type);
		if(found != m_texture_map.end())
			return found->second;

        assert(false && "Couldn't find texture with the specified texture type!");

        return nullptr;
    }
    
	glm::vec3 Material::getVector3(const std::string_view &uniform_name)
    {
		auto found = m_vec3_map.find(uniform_name);
		if(found != m_vec3_map.end())
			return found->second;

		return glm::vec3(0);
    }

	float Material::getFloat(const std::string_view &uniform_name)
    {
		auto found = m_float_map.find(uniform_name);
		if(found != m_float_map.end())
			return found->second;

		return 0.f;
    }

	bool Material::getBool(const std::string_view &uniform_name)
    {
		auto found = m_bool_map.find(uniform_name);
		if(found != m_bool_map.end())
			return found->second;

        return false;
    }
}
