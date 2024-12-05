#pragma once
#include "container_types.h"
#include "texture.h"

namespace RGL
{
    class Material
    {
    public:
        // Texture type order must match the order in pbr-lighting.glh
        // TextureType is being cast to uint32_t during the mesh rendering.
        enum class TextureType { ALBEDO, NORMAL, METALLIC, ROUGHNESS, AO, EMISSIVE };

        Material();
        ~Material();

		void set(TextureType texture_type, const std::shared_ptr<Texture2D>& texture);
		void set(const std::string_view& uniform_name, const glm::vec3& vector3);
		void set(const std::string_view& uniform_name, float value);
		void set(const std::string_view& uniform_name, bool value);

		std::shared_ptr<Texture2D> getTexture(TextureType texture_type);
		glm::vec3                  getVector3(const std::string_view& uniform_name);
		float                      getFloat  (const std::string_view& uniform_name);
		bool                       getBool   (const std::string_view& uniform_name);

    private:
		dense_map<TextureType, std::shared_ptr<Texture2D>> m_texture_map;
		string_map<glm::vec3>                  m_vec3_map;
		string_map<float>                      m_float_map;
		string_map<bool>                       m_bool_map;

        friend class StaticModel;
    };
}
