#pragma once

#include <cstdio>
#include <filesystem>
#include <print>
#include <utility>

namespace Log
{

using seconds_f = std::chrono::duration<float, std::ratio<1>>;

using Level = uint_fast8_t;
static constexpr Level DEBUG   = 10;
static constexpr Level INFO    = 20;
static constexpr Level WARNING = 30;
static constexpr Level ERROR   = 40;
static constexpr Level FATAL   = 50;


namespace _private
{
void preamble(Level lvl, FILE *fp);
void end(FILE *fp);

// settings
static auto  initialized  { true };
static Level level        { WARNING };
static Level error_level  { WARNING };  // at or higher also writes to stderr
static FILE  *out         { stdout };
static bool  output_date  { false };
static bool  output_since { false };
static std::chrono::steady_clock::time_point start_time;

template<typename... Args>
void log_msg(Level lvl, std::format_string<Args...> fmt, Args&&... args)
{
	_private::preamble(lvl, out);
	std::print(out, fmt, std::forward<Args>(args)...);
	_private::end(out);

	if(lvl >= error_level and out != stderr)
	{
		_private::preamble(lvl, stderr);
		std::print(stderr, fmt, std::forward<Args>(args)...);
		_private::end(stderr);
	}
}

} // _private

bool set_file(std::filesystem::path &file_path);
Level set_level(Level min_level);
void enable_date(bool enable);
void enable_since(bool enable);

template<typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args)
{
	if(_private::level <= DEBUG)
		_private::log_msg(DEBUG, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args)
{
	if(_private::level <= INFO)
		_private::log_msg(INFO, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void warning(std::format_string<Args...> fmt, Args&&... args)
{
	if(_private::level <= WARNING)
		_private::log_msg(WARNING, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args)
{
	if(_private::level <= ERROR)
		_private::log_msg(ERROR, fmt, std::forward<Args>(args)...);
}

void flush();
void close();

} // log
