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

#include "ssbo.h"

namespace RGL
{

using GroupsBuffer = ShaderStorageBuffer<glm::uvec3>;

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
	enum class Barrier : GLbitfield
	{
		None = 0,
		SSBO = GL_SHADER_STORAGE_BARRIER_BIT,
	};

	void enableLiveReload();

	Shader();
	explicit Shader(const std::filesystem::path & compute_shader_filepath, const string_set &conditionals={});

	Shader(const std::filesystem::path & vertex_shader_filepath,
		   const std::filesystem::path & fragment_shader_filepath,
		   const string_set &conditionals={});

	Shader(const std::filesystem::path & vertex_shader_filepath,
		   const std::filesystem::path & fragment_shader_filepath,
		   const std::filesystem::path & geometry_shader_filepath,
		   const string_set &conditionals={});

	Shader(const std::filesystem::path & vertex_shader_filepath,
		   const std::filesystem::path & fragment_shader_filepath,
		   const std::filesystem::path & tessellation_control_shader_filepath,
		   const std::filesystem::path & tessellation_evaluation_shader_filepath,
		   const string_set &conditionals={});

	Shader(const std::filesystem::path & vertex_shader_filepath,
		   const std::filesystem::path & fragment_shader_filepath,
		   const std::filesystem::path & geometry_shader_filepath,
		   const std::filesystem::path & tessellation_control_shader_filepath,
		   const std::filesystem::path & tessellation_evaluation_shader_filepath,
		   const string_set &conditionals={});

	~Shader();

	bool link();
	void bind() const;
	void setTransformFeedbackVaryings(const std::vector<const char*>& output_names, GLenum buffer_mode) const;
	std::string_view name() const { return _name; }

	void setPreBarrier(Barrier barrier_bits);
	void setPostBarrier(Barrier barrier_bits);

	void invoke(size_t groups_x=1, size_t groups_y=1, size_t groups_z=1);
	inline void invoke(glm::uvec2 groups) { invoke(groups.x, groups.y); }
	inline void invoke(glm::uvec3 groups) { invoke(groups.x, groups.y, groups.z); }
	void invoke(const GroupsBuffer &groups, size_t offset=0);

	void setUniform(const std::string_view & name, float value);
	void setUniform(const std::string_view & name, int value);
	void setUniform(const std::string_view & name, unsigned int value);
	void setUniform(const std::string_view & name, size_t count, const float * values);
	void setUniform(const std::string_view & name, size_t count, const int * values);
	void setUniform(const std::string_view & name, size_t count, const glm::vec2 * vectors);
	void setUniform(const std::string_view & name, size_t count, const glm::vec3 * vectors);
	void setUniform(const std::string_view & name, size_t count, const glm::mat4 * matrices);
	void setUniform(const std::string_view & name, size_t count, const glm::mat2x4 * matrices);
	void setUniform(const std::string_view & name, const std::vector<float> &values);
	void setUniform(const std::string_view & name, const std::vector<int> &values);
	void setUniform(const std::string_view & name, const std::vector<glm::vec2> &vectors);
	void setUniform(const std::string_view & name, const std::vector<glm::vec3> &vectors);
	void setUniform(const std::string_view & name, const std::vector<glm::mat4> &matrices);
	void setUniform(const std::string_view & name, const std::vector<glm::mat2x4> &matrices);
	void setUniform(const std::string_view & name, const glm::vec2 & vector);
	void setUniform(const std::string_view & name, const glm::vec3 & vector);
	void setUniform(const std::string_view & name, const glm::vec4 & vector);
	void setUniform(const std::string_view & name, const glm::uvec2 & vector);
	void setUniform(const std::string_view & name, const glm::uvec3 & vector);
	void setUniform(const std::string_view & name, const glm::mat3 & matrix);
	void setUniform(const std::string_view & name, const glm::mat4 & matrix);

	void setSubroutine(ShaderType shader_type, const std::string& subroutine_name);

	inline operator bool () const { return m_program_id > 0 and m_failed_shaders == 0 and m_is_linked; }

private:
	void addAllSubroutines();
	
	bool addShader(const std::filesystem::path & filepath, GLuint type, const string_set &conditionals);
	bool loadShader(GLuint shaderObject, const std::filesystem::path &filepath, const string_set &conditionals);
	void logLineErrors(const std::filesystem::path &filepath, const std::string &log) const;
	void add_name(const std::filesystem::path &filepath);
	std::tuple<bool, std::string> getStatusLog(GLuint object, GLenum statusType) const;

	GLint getUniformLocation(const std::string_view &name);

private:
	string_map<GLuint> m_subroutine_indices;
	dense_map<GLenum, GLuint> m_active_subroutine_uniform_locations;

	string_map<GLint> m_uniforms_locations;

	struct ShaderItem
	{
		GLuint shaderObject;
		string_set conditionals;
	};
	string_map<ShaderItem> _shaderFiles;

	GLuint m_program_id;
	bool m_is_linked;
	size_t m_failed_shaders;
	std::string _name;
	Barrier _pre_barrier { Barrier::None };
	Barrier _post_barrier { Barrier::None };

};

} // RGL
