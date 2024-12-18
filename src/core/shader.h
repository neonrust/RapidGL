#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include "container_types.h"

namespace RGL
{
    class Shader final
    {
    public:
        enum class ShaderType 
        { 
            VERTEX                 = GL_VERTEX_SHADER, 
            FRAGMENT               = GL_FRAGMENT_SHADER,
            GEOMETRY               = GL_GEOMETRY_SHADER, 
            TESSELATION_CONTROL    = GL_TESS_CONTROL_SHADER,
            TESSELATION_EVALUATION = GL_TESS_EVALUATION_SHADER, 
            COMPUTE                = GL_COMPUTE_SHADER
        };

        Shader();
        explicit Shader(const std::filesystem::path & compute_shader_filepath);

        Shader(const std::filesystem::path & vertex_shader_filepath,
               const std::filesystem::path & fragment_shader_filepath);

        Shader(const std::filesystem::path & vertex_shader_filepath,
               const std::filesystem::path & fragment_shader_filepath,
               const std::filesystem::path & geometry_shader_filepath);

        Shader(const std::filesystem::path & vertex_shader_filepath,
               const std::filesystem::path & fragment_shader_filepath,
               const std::filesystem::path & tessellation_control_shader_filepath,
               const std::filesystem::path & tessellation_evaluation_shader_filepath);

        Shader(const std::filesystem::path & vertex_shader_filepath,
               const std::filesystem::path & fragment_shader_filepath,
               const std::filesystem::path & geometry_shader_filepath,
               const std::filesystem::path & tessellation_control_shader_filepath,
               const std::filesystem::path & tessellation_evaluation_shader_filepath);

        ~Shader();

        bool link();
        void setTransformFeedbackVaryings(const std::vector<const char*>& output_names, GLenum buffer_mode) const;
        void bind() const;
		std::string_view name() const { return _name; }

		void setUniform(const std::string_view & name, float value);
		void setUniform(const std::string_view & name, int value);
		void setUniform(const std::string_view & name, GLuint value);
		void setUniform(const std::string_view & name, GLsizei count, const float * values);
		void setUniform(const std::string_view & name, GLsizei count, const int * values);
		void setUniform(const std::string_view & name, GLsizei count, const glm::vec3 * vectors);
		void setUniform(const std::string_view & name, const glm::vec2 & vector);
		void setUniform(const std::string_view & name, const glm::vec3 & vector);
		void setUniform(const std::string_view & name, const glm::vec4 & vector);
		void setUniform(const std::string_view & name, const glm::uvec2 & vector);
		void setUniform(const std::string_view & name, const glm::uvec3 & vector);
		void setUniform(const std::string_view & name, const glm::mat3 & matrix);
		void setUniform(const std::string_view & name, const glm::mat4 & matrix);
		void setUniform(const std::string_view & name, const float* values, GLsizei count);
		void setUniform(const std::string_view & name, const glm::vec2* values, GLsizei count);
		void setUniform(const std::string_view & name, const glm::mat4 * matrices, GLsizei count);
		void setUniform(const std::string_view & name, const glm::mat2x4 * matrices, GLsizei count);

        void setSubroutine(ShaderType shader_type, const std::string& subroutine_name);

    private:
        void addAllSubroutines();

		bool addShader(const std::filesystem::path & filepath, GLuint type);
		void add_name(const std::filesystem::path &filepath);
		std::tuple<bool, std::string> getStatusLog(GLuint object, GLenum statusType) const;

		GLint getUniformLocation(const std::string_view &name);

		string_map<GLuint> m_subroutine_indices;
		dense_map<GLenum, GLuint> m_active_subroutine_uniform_locations;

		string_map<GLint> m_uniforms_locations;

        GLuint m_program_id;
        bool m_is_linked;
		std::string _name;
	};
}
