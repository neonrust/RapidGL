#include <glm/gtc/type_ptr.hpp>

#include "filesystem.h"
#include "shader.h"
#include "util.h"

#include <cstdio>

namespace RGL
{
    Shader::Shader()
		: m_program_id(0),   // allocated in first call to addShader()
          m_is_linked(false)
    {
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
		if (m_program_id)
        {
            glDeleteProgram(m_program_id);
            m_program_id = 0;
        }
    }

	bool Shader::addShader(const std::filesystem::path & filepath, GLuint type)
    {
        if (filepath.empty())
        {
			std::fprintf(stderr, "Error: Shader's file name can't be empty.\n");
			return false;
        }

		if(not m_program_id)
		{
			m_program_id = glCreateProgram();
			if(not m_program_id)
			{
				std::fprintf(stderr, "Error while creating program object.\n");
				return false;
			}
		}

		auto shaderObject = glCreateShader(type);
		if(not shaderObject)
        {
			std::fprintf(stderr, "Error while creating %s.\n", filepath.c_str());
			return false;
        }

		const auto  dir = FileSystem::rootPath() / filepath.parent_path();
		const auto code = Util::PreprocessShaderSource(Util::LoadFile(filepath), dir);

		const char * shader_code = code.c_str();

		add_name(filepath);

		glShaderSource(shaderObject, 1, &shader_code, nullptr);
        glCompileShader(shaderObject);

		const auto &[ok, log] = getStatusLog(shaderObject, GL_COMPILE_STATUS);
		if(not ok)
		{

			if(not log.empty())
			{
				logLineErrors(filepath, log);
				std::fprintf(stderr, "%s Compilation failed!\n", filepath.string().c_str());
			}
			else
				std::fprintf(stderr, "%s Compilation failed!\n", filepath.string().c_str());
			return false;
        }

        glAttachShader(m_program_id, shaderObject);
		glDeleteShader(shaderObject); // flag for deletion; we don't need it for anything else

		return true;
    }

	void Shader::add_name(const std::filesystem::path & filepath)
	{
		if(not _name.empty())
			_name.append(";");

		_name += filepath.filename().string();
	}

    bool Shader::link()
    {
        glLinkProgram(m_program_id);

		const auto &[ok, log] = getStatusLog(m_program_id, GL_LINK_STATUS);
		if(not ok)
		{
			if(not log.empty())
			{
				std::fprintf(stderr, "%s Linking failed!\n", _name.c_str());
				logLineErrors(_name, log);
			}
			else
				std::fprintf(stderr, "%s Linking failed!\n", _name.c_str());
			return false;
        }
        else
        {
            m_is_linked = true;

            addAllSubroutines();
        }

        return m_is_linked;
    }

	void Shader::logLineErrors(const std::filesystem::path & filepath, const std::string &log) const
	{
		// std::istringstream source_strm(source);

		// auto source_line = [&source_strm](auto line_num) -> std::string {
		// 	std::string line;
		// 	source_strm.seekg(0);
		// 	for(auto idx = 0; idx < line_num; ++idx)
		// 	{
		// 		if(not std::getline(source_strm, line))
		// 			return "** EOF **";
		// 	}
		// 	return line;
		// };

	   // error message syntax:
	   //    "0(<line num>) : "
		std::istringstream strm(log);
		std::string line;
		while(std::getline(strm, line))
		{
			if(line.size() < 10 or not line.starts_with("0("))
			{
				std::fprintf(stderr, "%s\n", line.c_str());
				continue;
			}

			const auto end_bracket = line.find(')', 2);
			if(end_bracket == std::string::npos)
			{
				std::fprintf(stderr, "%s\n", line.c_str());
				continue;
			}

			line = line.replace(0, 1, filepath);
			std::fprintf(stderr, "%s\n", line.c_str());

			// const auto line_num = std::stoi(line.substr(2, end_bracket - 2));
			// const auto src_line = source_line(line_num);
			// std::fprintf(stderr, ">%s\n", src_line.c_str());
		}
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
			m_uniforms_locations[name] = location; // also remember failures (calls will be ignored)
			if(location == -1)
				std::fprintf(stderr, "Shader[%s]: Uniform '%s' not found\n", _name.c_str(), name.data());
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

	void Shader::setUniform(const std::string_view &name, GLsizei count, const glm::vec2 *vectors)
	{
		glProgramUniform2fv(m_program_id, getUniformLocation(name), count, glm::value_ptr(vectors[0]));
	}

	void Shader::setUniform(const std::string_view & name, GLsizei count, const glm::vec3 *vectors)
    {
		assert(vectors);
		glProgramUniform3fv(m_program_id, getUniformLocation(name), count, glm::value_ptr(vectors[0]));
    }

	void Shader::setUniform(const std::string_view &name, GLsizei count, const glm::mat4 *matrices)
	{
		assert(matrices);
		glProgramUniformMatrix4fv(m_program_id, getUniformLocation(name), count, GL_FALSE, glm::value_ptr(matrices[0]));
	}

	void Shader::setUniform(const std::string_view &name, GLsizei count, const glm::mat2x4 *matrices)
	{
		assert(matrices);
		glProgramUniformMatrix2x4fv(m_program_id, getUniformLocation(name), count, GL_FALSE, glm::value_ptr(matrices[0]));
	}

	void Shader::setUniform(const std::string_view & name, const std::vector<float> &values)
	{
		glProgramUniform1fv(m_program_id, getUniformLocation(name), GLsizei(values.size()), values.data());
	}

	void Shader::setUniform(const std::string_view & name, const std::vector<int> &values)
	{
		glProgramUniform1iv(m_program_id, getUniformLocation(name), GLsizei(values.size()), &values[0]);
	}

	void Shader::setUniform(const std::string_view &name, const std::vector<glm::vec2> &vectors)
	{
		glProgramUniform2fv(m_program_id, getUniformLocation(name), GLsizei(vectors.size()), glm::value_ptr(vectors[0]));
	}

	void Shader::setUniform(const std::string_view & name, const std::vector<glm::vec3> &vectors)
	{
		glProgramUniform3fv(m_program_id, getUniformLocation(name), GLsizei(vectors.size()), glm::value_ptr(vectors[0]));
	}

	void Shader::setUniform(const std::string_view &name, const std::vector<glm::mat4> &matrices)
	{
		glProgramUniformMatrix4fv(m_program_id, getUniformLocation(name), GLsizei(matrices.size()), GL_FALSE, glm::value_ptr(matrices[0]));
	}

	void Shader::setUniform(const std::string_view &name, const std::vector<glm::mat2x4> &matrices)
	{
		glProgramUniformMatrix2x4fv(m_program_id, getUniformLocation(name), GLsizei(matrices.size()), GL_FALSE, glm::value_ptr(matrices[0]));
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

	// void Shader::setUniform(const std::string_view & name, const float *values, GLsizei count)
 //    {
	// 	glProgramUniform1fv(m_program_id, getUniformLocation(name), count, &values[0]);
 //    }

	// void Shader::setUniform(const std::string_view& name, const glm::vec2 *values, GLsizei count)
 //    {
	// 	glProgramUniform2fv(m_program_id, getUniformLocation(name), count, &values[0][0]);
 //    }

	// void Shader::setUniform(const std::string_view & name, const glm::mat4 *matrices, GLsizei count)
 //    {
	// 	glProgramUniformMatrix4fv(m_program_id, getUniformLocation(name), count, GL_FALSE, &matrices[0][0][0]);
 //    }

	// void Shader::setUniform(const std::string_view &name, const glm::mat2x4 *matrices, GLsizei count)
 //    {
	// 	glProgramUniformMatrix2x4fv(m_program_id, getUniformLocation(name), count, GL_FALSE, &matrices[0][0][0]);
 //    }

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

	std::tuple<bool, std::string> Shader::getStatusLog(GLuint object, GLenum statusType) const
	{
		const auto isShader = glIsShader(object);
		if(not isShader and not glIsProgram(object))
		{
			std::fprintf(stderr, "getStatusLog provided object is neither program nor shader: %d\n", object);
			return { false, {} };
		}

		const auto get_iv = isShader? glGetShaderiv: glGetProgramiv;
		const auto get_log = isShader? glGetShaderInfoLog: glGetProgramInfoLog;

		GLint status_ok = GL_FALSE;
		get_iv(object, statusType, &status_ok);

		if(not status_ok)
		{
			GLint logLen { 0 };
			get_iv(object, GL_INFO_LOG_LENGTH, &logLen);

			if(logLen)
			{
				std::string log;
				log.resize(size_t(logLen));

				GLsizei written;
				get_log(object, logLen, &written, log.data());
				return { false, log };
			}

			return { false, {} };

		}

		return { true, {} };
	}
}
