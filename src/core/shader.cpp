#include <glm/gtc/type_ptr.hpp>

#include "filesystem.h"
#include "shader.h"
#include "util.h"

namespace RGL
{
    Shader::Shader()
        : m_program_id(0),
          m_is_linked(false)
    {
		m_program_id = glCreateProgram(); // TODO: defer until shader added? (default ctor shouldn't allocate stuff)

        if (m_program_id == 0)
        {
            fprintf(stderr, "Error while creating program object.\n");
        }
    }

    Shader::Shader(const std::filesystem::path& compute_shader_filepath)
        : Shader()
    {
        addShader(compute_shader_filepath, GL_COMPUTE_SHADER);
    }

    Shader::Shader(const std::filesystem::path & vertex_shader_filepath,
                   const std::filesystem::path & fragment_shader_filepath)
        : Shader()
    {
        addShader(vertex_shader_filepath, GL_VERTEX_SHADER);
        addShader(fragment_shader_filepath, GL_FRAGMENT_SHADER);
    }

    Shader::Shader(const std::filesystem::path & vertex_shader_filepath,
                   const std::filesystem::path & fragment_shader_filepath,
                   const std::filesystem::path & geometry_shader_filepath)
        : Shader(vertex_shader_filepath, fragment_shader_filepath)
    {
        addShader(geometry_shader_filepath, GL_GEOMETRY_SHADER);
    }

    Shader::Shader(const std::filesystem::path & vertex_shader_filepath,
                   const std::filesystem::path & fragment_shader_filepath,
                   const std::filesystem::path & tessellation_control_shader_filepath,
                   const std::filesystem::path & tessellation_evaluation_shader_filepath)
        : Shader(vertex_shader_filepath, fragment_shader_filepath)
    {
        addShader(tessellation_control_shader_filepath, GL_TESS_CONTROL_SHADER);
        addShader(tessellation_evaluation_shader_filepath, GL_TESS_EVALUATION_SHADER);
    }

    Shader::Shader(const std::filesystem::path & vertex_shader_filepath,
                   const std::filesystem::path & fragment_shader_filepath,
                   const std::filesystem::path & geometry_shader_filepath,
                   const std::filesystem::path & tessellation_control_shader_filepath,
                   const std::filesystem::path & tessellation_evaluation_shader_filepath)
        : Shader(vertex_shader_filepath,
                 fragment_shader_filepath,
                 tessellation_control_shader_filepath,
                 tessellation_evaluation_shader_filepath)
    {
        addShader(geometry_shader_filepath, GL_GEOMETRY_SHADER);
    }

    Shader::~Shader()
    {
        if (m_program_id != 0)
        {
            glDeleteProgram(m_program_id);
            m_program_id = 0;
        }
    }

    void Shader::addShader(const std::filesystem::path & filepath, GLuint type) const
    {
        if (m_program_id == 0)
        {
            return;
        }

        if (filepath.empty())
        {
            fprintf(stderr, "Error: Shader's file name can't be empty.\n");

            return;
        }

        GLuint shaderObject = glCreateShader(type);

        if (shaderObject == 0)
        {
            fprintf(stderr, "Error while creating %s.\n", filepath.c_str());

            return;
        }


        std::string           code = Util::LoadFile(filepath);
        std::filesystem::path dir  = FileSystem::getRootPath() / filepath.parent_path();

        code = Util::LoadShaderIncludes(code, dir);

        const char * shader_code = code.c_str();

        glShaderSource(shaderObject, 1, &shader_code, nullptr);
        glCompileShader(shaderObject);

        GLint result;
        glGetShaderiv(shaderObject, GL_COMPILE_STATUS, &result);

        if (result == GL_FALSE)
        {
            fprintf(stderr, "\n%s compilation failed!\n", filepath.string().c_str());

            GLint logLen;
            glGetShaderiv(shaderObject, GL_INFO_LOG_LENGTH, &logLen);

            if (logLen > 0)
            {
				std::string log;
				log.resize(size_t(logLen));

                GLsizei written;
				glGetShaderInfoLog(shaderObject, logLen, &written, log.data());

				fprintf(stderr, "Shader log: \n%s", log.c_str());
            }
            getchar();
            return;
        }

        glAttachShader(m_program_id, shaderObject);
        glDeleteShader(shaderObject);
    }

    bool Shader::link()
    {
        glLinkProgram(m_program_id);

        GLint status;
        glGetProgramiv(m_program_id, GL_LINK_STATUS, &status);

        if (status == GL_FALSE)
        {
            fprintf(stderr, "Failed to link shader program!\n");

            GLint logLen;
            glGetProgramiv(m_program_id, GL_INFO_LOG_LENGTH, &logLen);

            if (logLen > 0)
            {
				std::string log;
				log.resize(size_t(logLen));

				GLsizei written;
				glGetProgramInfoLog(m_program_id, logLen, &written, log.data());

				fprintf(stderr, "Program log: \n%s", log.c_str());
            }
        }
        else
        {
            m_is_linked = true;

            addAllSubroutines();
        }

        return m_is_linked;
    }

    void Shader::setTransformFeedbackVaryings(const std::vector<const char*>& output_names, GLenum buffer_mode) const
    {
		glTransformFeedbackVaryings(m_program_id, GLsizei(output_names.size()), output_names.data(), buffer_mode);
    }

    void Shader::bind() const
    {
        if (m_program_id != 0 && m_is_linked)
        {
            glUseProgram(m_program_id);
        }
    }

	GLint Shader::getUniformLocation(const std::string_view & name)
    {
		GLint location = -1;

		const auto found = m_uniforms_locations.find(name);
		if(found == m_uniforms_locations.end())
		{
			location = glGetUniformLocation(m_program_id, name.data());
			if(location != -1)
				m_uniforms_locations[name] = location;
		}
		else
			location = found->second;

		return location;
    }

	void Shader::setUniform(const std::string_view & name, float value)
    {
		glProgramUniform1f(m_program_id, getUniformLocation(name), value);
    }

	void Shader::setUniform(const std::string_view & name, int value)
    {
		glProgramUniform1i(m_program_id, getUniformLocation(name), value);
    }

	void Shader::setUniform(const std::string_view & name, GLuint value)
    {
		glProgramUniform1ui(m_program_id, getUniformLocation(name), value);
    }

	void Shader::setUniform(const std::string_view & name, GLsizei count, const float *values)
    {
		assert(values);
		glProgramUniform1fv(m_program_id, getUniformLocation(name), count, values);
    }

	void Shader::setUniform(const std::string_view & name, GLsizei count, const int *values)
    {
		assert(values);
		glProgramUniform1iv(m_program_id, getUniformLocation(name), count, values);
    }

	void Shader::setUniform(const std::string_view & name, GLsizei count, const glm::vec3 *vectors)
    {
		assert(vectors);
		glProgramUniform3fv(m_program_id, getUniformLocation(name), count, glm::value_ptr(vectors[0]));
    }

	void Shader::setUniform(const std::string_view & name, const glm::vec2 & vector)
    {
		glProgramUniform2fv(m_program_id, getUniformLocation(name), 1, glm::value_ptr(vector));
    }

	void Shader::setUniform(const std::string_view & name, const glm::vec3 & vector)
    {
		glProgramUniform3fv(m_program_id, getUniformLocation(name), 1, glm::value_ptr(vector));
    }

	void Shader::setUniform(const std::string_view & name, const glm::vec4 & vector)
    {
		glProgramUniform4fv(m_program_id, getUniformLocation(name), 1, glm::value_ptr(vector));
    }

	void Shader::setUniform(const std::string_view & name, const glm::uvec2& vector)
    {
		glProgramUniform2uiv(m_program_id, getUniformLocation(name), 1, glm::value_ptr(vector));
    }

	void Shader::setUniform(const std::string_view & name, const glm::uvec3& vector)
    {
		glProgramUniform3uiv(m_program_id, getUniformLocation(name), 1, glm::value_ptr(vector));
    }

	void Shader::setUniform(const std::string_view & name, const glm::mat3 & matrix)
    {
		glProgramUniformMatrix3fv(m_program_id, getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
    }

	void Shader::setUniform(const std::string_view & name, const glm::mat4 & matrix)
    {
		glProgramUniformMatrix4fv(m_program_id, getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
    }

	void Shader::setUniform(const std::string_view & name, const float *values, GLsizei count)
    {
		glProgramUniform1fv(m_program_id, getUniformLocation(name), count, &values[0]);
    }

	void Shader::setUniform(const std::string_view& name, const glm::vec2 *values, GLsizei count)
    {
		glProgramUniform2fv(m_program_id, getUniformLocation(name), count, &values[0][0]);
    }

	void Shader::setUniform(const std::string_view & name, const glm::mat4 *matrices, GLsizei count)
    {
		glProgramUniformMatrix4fv(m_program_id, getUniformLocation(name), count, GL_FALSE, &matrices[0][0][0]);
    }

	void Shader::setUniform(const std::string_view &name, const glm::mat2x4 *matrices, GLsizei count)
    {
		glProgramUniformMatrix2x4fv(m_program_id, getUniformLocation(name), count, GL_FALSE, &matrices[0][0][0]);
    }

    void Shader::setSubroutine(ShaderType shader_type, const std::string & subroutine_name)
    {
		glUniformSubroutinesuiv(
			GLenum(shader_type),
			GLsizei(m_active_subroutine_uniform_locations[GLenum(shader_type)]),
			&m_subroutine_indices[subroutine_name]
		);
    }

    void Shader::addAllSubroutines()
    {
        GLenum interfaces[]    = { GL_VERTEX_SUBROUTINE, GL_FRAGMENT_SUBROUTINE };
        GLenum shader_stages[] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };

        GLint interfaces_count = std::size(interfaces);

		for(GLint if_idx = 0; if_idx < interfaces_count; ++if_idx)
        {
            /* Get all active subroutines */
			GLenum program_interface = interfaces[if_idx];

			GLuint num_subroutines = 0;
			glGetProgramInterfaceiv(m_program_id, program_interface, GL_ACTIVE_RESOURCES, (GLint *)&num_subroutines);

            const GLenum properties[] = { GL_NAME_LENGTH };
            const GLint properties_size = sizeof(properties) / sizeof(properties[0]);

            GLint count_subroutine_locations = 0;
			glGetProgramStageiv(m_program_id, shader_stages[if_idx], GL_ACTIVE_SUBROUTINE_UNIFORM_LOCATIONS, &count_subroutine_locations);
			m_active_subroutine_uniform_locations[shader_stages[if_idx]] = GLuint(count_subroutine_locations);

			for (GLuint sub_idx = 0; sub_idx < num_subroutines; ++sub_idx)
            {
                GLint values[properties_size];
                GLint length = 0;
				glGetProgramResourceiv(m_program_id, program_interface, sub_idx, properties_size, properties, properties_size, &length, values);

				std::vector<char> name_data(static_cast<unsigned int>(values[0]));
				glGetProgramResourceName(m_program_id, program_interface, sub_idx, name_data.size(), nullptr, &name_data[0]);
                std::string subroutine_name(name_data.begin(), name_data.end() - 1);

				GLuint subroutine_index = glGetSubroutineIndex(m_program_id, shader_stages[if_idx], subroutine_name.c_str());

                m_subroutine_indices[subroutine_name] = subroutine_index;
            }
        }
    }
}
