#include <glm/gtc/type_ptr.hpp>

#include "shader.h"
#include "util.h"
#include "log.h"

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
		ShaderRegistry::the().remove(m_program_id);
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
		Log::warning("Shader::monitorFile() NOT IMPLEMENTED: {}", filepath);
		// Util::MonitorFileChanges(filepath, [this, filepath, item]() {
		// 	loadShader(fs::path(filepath), item.shaderObject, item.conditionals);
		// });
	}
}

bool Shader::addShader(const std::filesystem::path & filepath, ShaderType type, const string_set &conditionals)
{
	if (filepath.empty())
	{
		Log::error("Error: Shader's file name can't be empty.");
		return false;
	}

	if(not m_program_id)
	{
		m_program_id = glCreateProgram();
		if(not m_program_id)
		{
			Log::error("Error while creating program object.");
			return false;
		}
		ShaderRegistry::the().add(m_program_id, this);
	}

	auto shaderObject = glCreateShader(GLenum(type));
	if(not shaderObject)
	{
		Log::error("Error while creating shader object (type {}).", GLenum(type));
		return false;
	}

	_shaderFiles[filepath.string()] = ShaderItem{ shaderObject, conditionals };

	return loadShader(shaderObject, type, filepath, conditionals);
}

bool Shader::loadShader(GLuint shaderObject, ShaderType type, const std::filesystem::path &filepath, const string_set &conditionals)
{
	auto [code, ok] = Util::LoadShaderFile(filepath);
	if(not ok)
	{
		Log::error("Load shader failed: {}", filepath.string());
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
			Log::error("{} Compilation failed!", filepath.string());
			if(not log.empty())
				logLineErrors(filepath, log, { macros, code }, 10);
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
			Log::error("Shader[{}]: linking failed!", _name);
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
		if(duration > 4ms)
			Log::info("Shader[{} / {}]: linked, in {} ms", m_program_id, _name, duration_cast<milliseconds>(duration));
		else
			Log::info("Shader[{} / {}]: linked, in {} µs", m_program_id, _name, duration_cast<microseconds>(duration));
	}

	return m_is_linked;
}

void Shader::logLineErrors(const std::filesystem::path & filepath, const std::string &log, const std::array<std::string_view, 2> &sources, size_t max_errors) const
{
	auto errors_with_context = 2;

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
			if(line.size() < 10)
			{
				Log::error("{}", line);
				continue;
			}
			auto start_source_line = line.find("0(");
			if(start_source_line == std::string::npos)
			{
				Log::error("{}", line);
				continue;
			}

			auto file_line = line;
			file_line.replace(start_source_line, start_source_line + 1, filepath);
			Log::error("{}", file_line);

			if(errors_with_context)
			{
				--errors_with_context;
				const auto start_line_num = start_source_line + 2;
				auto line_v = std::string_view(line).substr(start_line_num);
				int line_num { -1 };
				const auto parse_result = std::from_chars(line_v.data(), line_v.data() + line_v.size(), line_num);
				if(parse_result.ec == std::errc{} and line_num > -1)
				{
					const size_t context = 1;
					size_t pre_context = context;
					auto lines = get_source_lines(sources, size_t(line_num) - 1, context, pre_context);
					if(not lines.empty())
					{
						using u32 = uint32_t;

							   // line number width; for alignment
						const auto width = 1 + u32(std::floor(std::log10(lines.size() - 1 + u32(line_num) - pre_context)));

						for(auto idx = 0u; idx < pre_context; ++idx)
							Log::error("{:{}}>{}", idx + u32(line_num) - pre_context, width, lines[idx]);
						Log::error("\x1b[1m{:{}}>{}\x1b[m", line_num, width, lines[pre_context]);
						for(auto idx = 0u; idx < context; ++idx)
							Log::error("{:{}}>{}", idx + u32(line_num) + context, width, lines[pre_context + 1 + idx]);
					}
				}
			}
		}
	}

	if(capped)
		Log::error("(+{} errors)", num_errors - max_errors);
}

static size_t num_source_lines(std::string_view source)
{
	const auto trailing_line = not source.empty() and source[source.size() - 1] != '\n'? 1u: 0u;
	return size_t(std::count(source.begin(), source.end(), '\n')) + trailing_line;
}

class string_reader
{
public:
	explicit string_reader(std::string_view s) : _str(s) {}

	std::string_view next_line()
	{
		if(is_end()) // already end of string
			return {};  // empty, not even lf -> end of string

		auto lf = _str.find('\n', _pos);
		if(lf == std::string_view::npos)
			lf = _str.size(); // i.e. the rest of the string

		auto line = _str.substr(_pos, lf + 1 - _pos); // including LF
		_pos = lf + 1; // start of next line (after lf), might be outside string (i.e. end state)

		return line;
	}
	inline bool is_end() const { return _pos >= _str.size(); }

private:
	std::string_view _str;
	size_t _pos = 0;
};

std::vector<std::string_view> Shader::get_source_lines(const std::array<std::string_view, 2> &sources, size_t line_num, size_t context_lines, size_t &before_context)
{
	const auto lines_source0 = num_source_lines(sources[0]);
	const auto lines_source1 = num_source_lines(sources[1]);

	// number of context lines before the desired line (might be less than 'context_lines')
	before_context = context_lines >= line_num? 0: context_lines;
	auto first_line = line_num - before_context;
	auto last_line  = std::min(line_num + context_lines, lines_source1 - 1);
	auto lines_remaining = last_line - first_line + 1;

	std::vector<std::string_view> lines;
	lines.reserve(lines_remaining);

	auto lines0 = string_reader(sources[0]);
	auto lines1 = string_reader(sources[1]);

	// skip leading lines
	auto skipped = 0ul;
	if(first_line >= lines_source0)
		skipped = lines_source0; // skip all of sources[0]; no need to read it
	else
	{
		for(; skipped < first_line and not lines0.is_end(); ++skipped)
			if(lines0.next_line().empty())
				return {}; // unexpected eod of string
	}
	if(skipped < first_line)
	{
		for(; skipped < first_line and not lines1.is_end(); ++skipped)
			if(lines1.next_line().empty())
				return {}; // unexpected eod of string
	}
	assert(skipped == first_line);

	// lines from sources[0] (likely none)
	for(auto idx = first_line; idx < first_line + before_context and idx <= lines_source0 and lines_remaining; ++idx, --lines_remaining)
	{
		const auto line = lines0.next_line();
		assert(not line.empty());
		lines.push_back(line.substr(0, line.size() - 1));
	}

	// lines from sources[1]
	while(lines_remaining--)
	{
		const auto line = lines1.next_line();
		assert(not line.empty());
		lines.push_back(line.substr(0, line.size() - 1));
	}

	return lines;
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
			Log::warning("Shader[{}]: Uniform not found: {}", _name, name);
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
		Log::error("getStatusLog provided object is neither program nor shader: {}", object);
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
