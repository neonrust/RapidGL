#include "material.h"

#include <string_view>
using namespace std::literals;

#include "hash_combine.h"
#include "hash_vec3.h"


namespace RGL
{
    Material::Material()
    {
		m_vec3_map.reserve(4);
		m_float_map.reserve(4);
		m_bool_map.reserve(8);

		set("u_albedo"sv,            glm::vec3(1.0f));
		set("u_emission"sv,          glm::vec3(0.0f));
		set("u_emission_strength"sv, 1.f);
		set("u_ao"sv,                1.0f);
		set("u_roughness"sv,         0.0f);
		set("u_metallic"sv,          0.0f);
		set("u_has_albedo_map"sv,    false);
		set("u_has_normal_map"sv,    false);
		set("u_has_emissive_map"sv,  false);
		set("u_has_ao_map"sv,        false);
		set("u_has_metallic_map"sv,  false);
		set("u_has_roughness_map"sv, false);
    }

    Material::~Material()
    {
    }
	
	void Material::set(TextureType texture_type, std::string_view name, const std::shared_ptr<const Texture2D>& texture)
    {
        m_texture_map[texture_type] = texture;
		_texture_names[texture_type] = name;
		_hash_dirty = true;
    }
	
	void Material::set(std::string_view uniform_name, const glm::vec3& vec)
    {
		m_vec3_map[uniform_name] = vec;
		_hash_dirty = true;
	}

	void Material::set(std::string_view uniform_name, float value)
    {
        m_float_map[uniform_name] = value;
		_hash_dirty = true;
	}

	void Material::set(std::string_view uniform_name, bool value)
    {
        m_bool_map[uniform_name] = value;
		_hash_dirty = true;
	}

	std::shared_ptr<const Texture2D> Material::getTexture(TextureType texture_type)
    {
		auto found = m_texture_map.find(texture_type);
		if(found != m_texture_map.end())
			return found->second;

        assert(false && "Couldn't find texture with the specified texture type!");

        return nullptr;
    }

	std::string_view Material::textureName(TextureType texture_type) const
	{
		auto found = _texture_names.find(texture_type);
		if(found != _texture_names.end())
			return found->second;

		return {};
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

	size_t Material::hash() const
	{
		if(_hash_dirty)
		{
			// TODO: update `_hash`
			size_t h { 0 };
			for(const auto texture_type: { TextureType::ALBEDO, TextureType::NORMAL, TextureType::METALLIC, TextureType::ROUGHNESS, TextureType::AO, TextureType::EMISSIVE })
				h = hash_combine(h, std::hash<std::string_view>{}(textureName(texture_type)));

			for(const auto &[name, value]: m_vec3_map)
			{
				h = hash_combine(h, std::hash<std::string_view>{}(name));
				h = hash_combine(h, std::hash<glm::vec3>{}(value));
			}
			for(const auto &[name, value]: m_float_map)
			{
				h = hash_combine(h, std::hash<std::string_view>{}(name));
				h = hash_combine(h, std::hash<float>{}(value));
			}
			for(const auto &[name, value]: m_bool_map)
			{
				h = hash_combine(h, std::hash<std::string_view>{}(name));
				h = hash_combine(h, std::hash<bool>{}(value));
			}
			_hash = h;
			_hash_dirty = false;
		}
		return _hash;
	}
}
