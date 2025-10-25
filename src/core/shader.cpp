#include <glm/gtc/type_ptr.hpp>

#include "filesystem.h"
#include "shader.h"
#include "util.h"

#include <print>
#include <chrono>
#include <ranges>

using namespace std::literals;
using namespace std::chrono;

namespace RGL
{

static constexpr auto s_link_log_threshold = milliseconds(20);

Shader::Shader() :
	m_program_id(0),   // allocated in first call to addShader()
	m_is_linked(false),
	m_failed_shaders(0)
{
}

Shader::Shader(const std::filesystem::path& compute_shader_filepath, const string_set &conditionals)
	: Shader()
{
	addShader(compute_shader_filepath, ShaderType::Compute, conditionals);
}

Shader::Shader(const std::filesystem::path & vertex_shader_filepath,
			   const std::filesystem::path & fragment_shader_filepath,
			   const string_set &conditionals)
	: Shader()
{
	addShader(vertex_shader_filepath, ShaderType::Vertex, conditionals);
	addShader(fragment_shader_filepath, ShaderType::Fragment, conditionals);
}

Shader::Shader(const std::filesystem::path & vertex_shader_filepath,
			   const std::filesystem::path & fragment_shader_filepath,
			   const std::filesystem::path & geometry_shader_filepath,
			   const string_set &conditionals)
	: Shader(vertex_shader_filepath,
			 fragment_shader_filepath,
			 conditionals)
{
	addShader(geometry_shader_filepath, ShaderType::Geometry, conditionals);
}

Shader::Shader(const std::filesystem::path & vertex_shader_filepath,
			   const std::filesystem::path & fragment_shader_filepath,
			   const std::filesystem::path & tessellation_control_shader_filepath,
			   const std::filesystem::path & tessellation_evaluation_shader_filepath,
			   const string_set &conditionals)
	: Shader(vertex_shader_filepath,
			 fragment_shader_filepath,
			 conditionals)
{
	addShader(tessellation_control_shader_filepath, ShaderType::TesselationControl, conditionals);
	addShader(tessellation_evaluation_shader_filepath, ShaderType::TesselationEvaluation, conditionals);
}

Shader::Shader(const std::filesystem::path & vertex_shader_filepath,
			   const std::filesystem::path & fragment_shader_filepath,
			   const std::filesystem::path & geometry_shader_filepath,
			   const std::filesystem::path & tessellation_control_shader_filepath,
			   const std::filesystem::path & tessellation_evaluation_shader_filepath,
			   const string_set &conditionals)
	: Shader(vertex_shader_filepath,
			 fragment_shader_filepath,
			 tessellation_control_shader_filepath,
			 tessellation_evaluation_shader_filepath,
			 conditionals)
{
	addShader(geometry_shader_filepath, ShaderType::Geometry, conditionals);
}

Shader::~Shader()
{
	if (m_program_id)
	{
		glDeleteProgram(m_program_id);
		m_program_id = 0;
	}
}

void Shader::enableLiveReload()
{

	for(const auto &[filepath, item]: _shaderFiles)
	{
		// TODO: use inotify to monitor 'filepath' for changes
		//   if changed, after a delay, call loadShader()
		std::print(stderr, "Shader::monitorFile() NOT IMPLEMENTED: {}\n", filepath);
		// Util::MonitorFileChanges(filepath, [this, filepath, item]() {
		// 	loadShader(fs::path(filepath), item.shaderObject, item.conditionals);
		// });
	}
}

bool Shader::addShader(const std::filesystem::path & filepath, ShaderType type, const string_set &conditionals)
{
	if (filepath.empty())
	{
		std::print(stderr, "Error: Shader's file name can't be empty.\n");
		return false;
	}

	if(not m_program_id)
	{
		m_program_id = glCreateProgram();
		if(not m_program_id)
		{
			std::print(stderr, "Error while creating program object.\n");
			return false;
		}
	}

	auto shaderObject = glCreateShader(GLenum(type));
	if(not shaderObject)
	{
		std::print(stderr, "Error while creating shader object (type {}).\n", GLenum(type));
		return false;
	}

	_shaderFiles[filepath.string()] = ShaderItem{ shaderObject, conditionals };

	return loadShader(shaderObject, type, filepath, conditionals);
}

bool Shader::loadShader(GLuint shaderObject, ShaderType type, const std::filesystem::path &filepath, const string_set &conditionals)
{
	const auto  dir = FileSystem::rootPath() / filepath.parent_path();
	const auto &[file_content, okf] = Util::LoadFile(filepath);
	if(not okf)
	{
		std::print(stderr, "Load shader failed: {}\n", filepath.string());
		m_failed_shaders++;
		return false;
	}
	auto [code, okp] = Util::PreprocessShaderSource(file_content, dir);
	if(not okp)
	{
		std::print(stderr, "Preprocessing shader failed: {}\n", filepath.string());
		m_failed_shaders++;
		return false;
	}

	// TODO: only first time being called for this 'filepath'; i.e. not when reloading
	add_name(filepath, type);

	std::string macros;
	if(not conditionals.empty())
	{
		macros.reserve(conditionals.size() * 16);
		for(const auto &cond: conditionals)
		{
			macros.append("#define "sv);
			macros.append(cond);
			macros.append("\n"sv);
		}

		// move #version statement to 'macros' (it must be first in the concatenated source string)
		const auto version_at = code.find("#version");
		if(version_at != std::string::npos)
		{
			const auto version_end = code.find('\n', version_at);
			const auto version = code.substr(version_at, version_end - version_at + 1);
			code[version_at] = '/';
			code[version_at + 1] = '/';

			macros.insert(0, version);
		}
	}

	const char *shader_sources[2] = {
		macros.c_str(),
		code.c_str(),
	};

	glShaderSource(shaderObject, 2, shader_sources, nullptr);
	glCompileShader(shaderObject);

	GLint success = GL_FALSE;
	glGetShaderiv(shaderObject, GL_COMPILE_STATUS, &success);

	if(success == GL_FALSE)
	{
		m_failed_shaders++;
		const auto &[ok, log] = getStatusLog(shaderObject, GL_COMPILE_STATUS);
		if(not ok)
		{
			std::print(stderr, "{} Compilation failed!\n", filepath.string());
			if(not log.empty())
				logLineErrors(filepath, log, 10);
		}
		return false;
	}

	glAttachShader(m_program_id, shaderObject);
	glDeleteShader(shaderObject); // flag for deletion; we don't need it for anything else

	return true;
}

void Shader::add_name(const std::filesystem::path & filepath, ShaderType type)
{
	if(not _name.empty())
		_name.append(";");

	_name += filepath.filename().string();

	if(_shaderTypes.capacity() == 0)
		_shaderTypes.reserve(4);
	_shaderTypes.push_back(type);
}

bool Shader::link()
{
	const auto T0 = steady_clock::now();

	glLinkProgram(m_program_id);

	GLint success = GL_FALSE;
	glGetProgramiv(m_program_id, GL_LINK_STATUS, &success);
	m_is_linked = success == GL_TRUE;

	if(success == GL_FALSE)
	{
		const auto &[ok, log] = getStatusLog(m_program_id, GL_LINK_STATUS);
		if(not ok)
		{
			std::print(stderr, "Shader[{}]: linking failed!\n", _name);
			if(not log.empty())
				logLineErrors(_name, log);
		}
		return false;
	}
	else
		addAllSubroutines();

	{
		const auto T1 = steady_clock::now();
		const auto duration = T1 - T0;
		if(duration >= s_link_log_threshold)
		{
			std::print("Shader[{}]: linked, in ", _name);
			if(duration_cast<milliseconds>(duration).count() > 1)
				std::print("{} ms\n", duration_cast<milliseconds>(duration));
			else
				std::print("{} µs\n", duration_cast<microseconds>(duration));
		}
	}

	return m_is_linked;
}

void Shader::logLineErrors(const std::filesystem::path & filepath, const std::string &log, size_t max_errors) const
{
	std::istringstream strm(log);
	std::string line;
	auto num_errors = 0u;
	bool capped = false;
	while(std::getline(strm, line))
	{
		if(num_errors > max_errors)
			capped = true;
		++num_errors;

		if(not capped)
		{
			auto start_bracket = line.find_first_of("0(");
			if(line.size() < 10 or start_bracket == std::string::npos)
			{
				std::print(stderr, "{}\n", line);
				continue;
			}

			const auto end_bracket = line.find(')', 2);
			if(end_bracket == std::string::npos)
			{
				std::print(stderr, "{}\n", line);
				continue;
			}

			line = line.replace(0, 1, filepath);
			std::print(stderr, "{}\n", line);
		}
	}

	if(capped)
		std::print("(+{} errors)\n", num_errors - max_errors);

}

void Shader::setTransformFeedbackVaryings(const std::vector<const char*>& output_names, GLenum buffer_mode) const
{
	glTransformFeedbackVaryings(m_program_id, GLsizei(output_names.size()), output_names.data(), buffer_mode);
}

void Shader::bind() const
{
	if(*this)
		glUseProgram(m_program_id);
}

void Shader::setPreBarrier(Barrier::Bits barrier_bits)
{
	_pre_barrier = barrier_bits;
}

void Shader::setPostBarrier(Barrier::Bits barrier_bits)
{
	_post_barrier = barrier_bits;
}

void Shader::invoke(size_t groups_x, size_t groups_y, size_t groups_z)
{
	if(_pre_barrier != Barrier::None)
		glMemoryBarrier(GLbitfield(_pre_barrier));

	bind();
	glDispatchCompute(GLuint(groups_x), GLuint(groups_y), GLuint(groups_z));

	if(_post_barrier != Barrier::None)
		glMemoryBarrier(GLbitfield(_post_barrier));
}

void Shader::invoke(const GroupsBuffer &groups, size_t offset)
{
	if(_pre_barrier != Barrier::None)
		glMemoryBarrier(GLbitfield(_pre_barrier));

	bind();
	groups.bindIndirectDispatch();
	glDispatchComputeIndirect(GLintptr(offset));

	if(_post_barrier != Barrier::None)
		glMemoryBarrier(GLbitfield(_post_barrier));
}

#if defined(DEBUG)
void Shader::_dump_uniforms() const
{
	auto uniforms = listUniforms();
	if(not uniforms.empty())
	{
		std::sort(uniforms.begin(), uniforms.end(), [](const auto &A, const auto &B) {
			return A < B;
		});

		for(const auto &[index, uniform]: std::views::enumerate(uniforms))
			std::print("  #{}: {} {}\n", index, uniform_type_name(uniform.type), uniform.name);
	}
}
#endif

std::vector<Shader::UniformInfo> Shader::listUniforms() const
{
	GLint num_uniforms = 0;
	glGetProgramiv(m_program_id, GL_ACTIVE_UNIFORMS, &num_uniforms);

	std::vector<Shader::UniformInfo> uniforms;
	uniforms.reserve(size_t(num_uniforms));

	for(auto idx = 0u; idx < size_t(num_uniforms); ++idx)
	{
		char nameBuf[256];
		GLsizei nameLen = 0;
		GLint size = 0;
		GLenum type = 0;
		glGetActiveUniform(m_program_id, idx, sizeof(nameBuf), &nameLen, &size, &type, nameBuf);

		std::string name(nameBuf, size_t(nameLen));

		GLint location = uniformLocation(name);

		uniforms.emplace_back(name, location, UniformType(type));
	}

	return uniforms;
}

std::optional<Shader::UniformInfo> Shader::uniformInfo(std::string_view name) const
{
	const auto location = uniformLocation(name);
	if(location ==  -1)
		return std::nullopt;

	GLint size = 0;
	GLenum type = 0;
	glGetActiveUniform(m_program_id, GLuint(location), 0, nullptr, &size, &type, nullptr);

	return UniformInfo{ std::string(name), GLuint(location), UniformType(type) };
}

GLint Shader::uniformLocation(const std::string_view & name) const
{
	GLint location = -1;

	const auto found = m_uniforms_locations.find(name);
	if(found == m_uniforms_locations.end())
	{
		location = glGetUniformLocation(m_program_id, name.data());
		if(location == -1)
			std::print(stderr, "Shader[{}]: Uniform not found: {}\n", _name, name);
		m_uniforms_locations[name] = location; // also remember failures (calls will be ignored)
	}
	else
	{
		location = found->second;
#if defined(DEBUG)
// TODO: verify type
#endif
	}


	return location;
}

GLint Shader::attributeLocation(const std::string_view &name) const
{
	return glGetAttribLocation(m_program_id, name.data());
}

void Shader::setUniform(const std::string_view & name, float value)
{
	glProgramUniform1f(m_program_id, uniformLocation(name), value);
}

void Shader::setUniform(const std::string_view & name, int value)
{
	glProgramUniform1i(m_program_id, uniformLocation(name), value);
}

void Shader::setUniform(const std::string_view & name, unsigned int value)
{
	glProgramUniform1ui(m_program_id, uniformLocation(name), value);
}

void Shader::setUniform(const std::string_view & name, size_t count, const float *values)
{
	assert(values);
	glProgramUniform1fv(m_program_id, uniformLocation(name), GLsizei(count), values);
}

void Shader::setUniform(const std::string_view & name, size_t count, const int *values)
{
	assert(values);
	glProgramUniform1iv(m_program_id, uniformLocation(name), GLsizei(count), values);
}

void Shader::setUniform(const std::string_view &name, size_t count, const glm::vec2 *vectors)
{
	glProgramUniform2fv(m_program_id, uniformLocation(name), GLsizei(count), glm::value_ptr(vectors[0]));
}

void Shader::setUniform(const std::string_view & name, size_t count, const glm::vec3 *vectors)
{
	assert(vectors);
	glProgramUniform3fv(m_program_id, uniformLocation(name), GLsizei(count), glm::value_ptr(vectors[0]));
}

void Shader::setUniform(const std::string_view &name, size_t count, const glm::vec4 *vectors)
{
	assert(vectors);
	glProgramUniform4fv(m_program_id, uniformLocation(name), GLsizei(count), glm::value_ptr(vectors[0]));
}

void Shader::setUniform(const std::string_view &name, size_t count, const glm::mat4 *matrices)
{
	assert(matrices);
	glProgramUniformMatrix4fv(m_program_id, uniformLocation(name), GLsizei(count), GL_FALSE, glm::value_ptr(matrices[0]));
}

void Shader::setUniform(const std::string_view &name, size_t count, const glm::mat2x4 *matrices)
{
	assert(matrices);
	glProgramUniformMatrix2x4fv(m_program_id, uniformLocation(name), GLsizei(count), GL_FALSE, glm::value_ptr(matrices[0]));
}

void Shader::setUniform(const std::string_view & name, const std::vector<float> &values)
{
	glProgramUniform1fv(m_program_id, uniformLocation(name), GLsizei(values.size()), values.data());
}

void Shader::setUniform(const std::string_view & name, const std::vector<int> &values)
{
	glProgramUniform1iv(m_program_id, uniformLocation(name), GLsizei(values.size()), &values[0]);
}

void Shader::setUniform(const std::string_view &name, const std::vector<glm::vec2> &vectors)
{
	glProgramUniform2fv(m_program_id, uniformLocation(name), GLsizei(vectors.size()), glm::value_ptr(vectors[0]));
}

void Shader::setUniform(const std::string_view & name, const std::vector<glm::vec3> &vectors)
{
	glProgramUniform3fv(m_program_id, uniformLocation(name), GLsizei(vectors.size()), glm::value_ptr(vectors[0]));
}

void Shader::setUniform(const std::string_view &name, const std::vector<glm::vec4> &vectors)
{
	glProgramUniform4fv(m_program_id, uniformLocation(name), GLsizei(vectors.size()), glm::value_ptr(vectors[0]));
}

void Shader::setUniform(const std::string_view &name, const std::vector<glm::mat4> &matrices)
{
	glProgramUniformMatrix4fv(m_program_id, uniformLocation(name), GLsizei(matrices.size()), GL_FALSE, glm::value_ptr(matrices[0]));
}

void Shader::setUniform(const std::string_view &name, const std::vector<glm::mat2x4> &matrices)
{
	glProgramUniformMatrix2x4fv(m_program_id, uniformLocation(name), GLsizei(matrices.size()), GL_FALSE, glm::value_ptr(matrices[0]));
}

void Shader::setUniform(const std::string_view & name, const glm::vec2 & vector)
{
	glProgramUniform2fv(m_program_id, uniformLocation(name), 1, glm::value_ptr(vector));
}

void Shader::setUniform(const std::string_view & name, const glm::vec3 & vector)
{
	glProgramUniform3fv(m_program_id, uniformLocation(name), 1, glm::value_ptr(vector));
}

void Shader::setUniform(const std::string_view & name, const glm::vec4 & vector)
{
	glProgramUniform4fv(m_program_id, uniformLocation(name), 1, glm::value_ptr(vector));
}

void Shader::setUniform(const std::string_view &name, const glm::ivec2 &vector)
{
	glProgramUniform2iv(m_program_id, uniformLocation(name), 1, glm::value_ptr(vector));
}

void Shader::setUniform(const std::string_view &name, const glm::ivec3 &vector)
{
	glProgramUniform3iv(m_program_id, uniformLocation(name), 1, glm::value_ptr(vector));
}

void Shader::setUniform(const std::string_view & name, const glm::uvec2& vector)
{
	glProgramUniform2uiv(m_program_id, uniformLocation(name), 1, glm::value_ptr(vector));
}

void Shader::setUniform(const std::string_view & name, const glm::uvec3& vector)
{
	glProgramUniform3uiv(m_program_id, uniformLocation(name), 1, glm::value_ptr(vector));
}

void Shader::setUniform(const std::string_view & name, const glm::mat3 & matrix)
{
	glProgramUniformMatrix3fv(m_program_id, uniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
}

void Shader::setUniform(const std::string_view & name, const glm::mat4 & matrix)
{
	glProgramUniformMatrix4fv(m_program_id, uniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
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
			glGetProgramResourceName(m_program_id, program_interface, sub_idx, GLsizei(name_data.size()), nullptr, &name_data[0]);
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
		std::print(stderr, "getStatusLog provided object is neither program nor shader: {}\n", object);
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

std::string_view Shader::uniform_type_name(UniformType type) const
{
	switch(type)
	{
		case UniformType::Float          : return "float"sv;
		case UniformType::UnsignedInteger: return "uint"sv;
		case UniformType::Integer        : return "int"sv;
		case UniformType::Vec2           : return "vec2"sv;
		case UniformType::Vec3           : return "vec3"sv;
		case UniformType::Vec4           : return "vec4"sv;
		case UniformType::Matrix3        : return "mat3"sv;
		case UniformType::Matrix4        : return "mat4"sv;
		case UniformType::Sampler1D      : return "sampler1D"sv;
		case UniformType::Sampler2D      : return "sampler2D"sv;
		case UniformType::Sampler3D      : return "sampler3D"sv;
		case UniformType::SamplerCube    : return "samplerCube"sv;
		case UniformType::Sampler2DArray : return "sampler2DArray"sv;
		case UniformType::Image2D        : return "image2D"sv;
		case UniformType::Image3D        : return "image3D"sv;
	}
		return "unknown"sv;
}

} // RGL
