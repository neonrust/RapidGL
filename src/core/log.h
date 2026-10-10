#pragma once

#include <cstdio>
#include <filesystem>
#include <print>
#include <utility>

namespace Log
{

using Level = uint_fast8_t;
static constexpr Level DEBUG   = 10;
static constexpr Level INFO    = 20;
static constexpr Level WARNING = 30;
static constexpr Level ERROR   = 40;
static constexpr Level FATAL   = 50;


class _private
{
private:
	_private();
public:
	static inline _private &the() {
		static _private instance;
		return instance;
	}
	void preamble(Level lvl, FILE *fp);
	void end(FILE *fp);
	void level_style(Level lvl, FILE *fp);
	void reset_style(FILE *fp);
	void out_level(Level lvl, FILE *fp);
	void stamp(FILE *fp);
	void lf(FILE *fp);

	static void on_signal(int signum);

	template<typename... Args>
	void log_msg(Level lvl, std::format_string<Args...> fmt, Args&&... args)
	{
		log_msg(out, lvl, fmt, std::forward<Args>(args)...);
		if(also_stdout and out != stdout)
			log_msg(stdout, lvl, fmt, std::forward<Args>(args)...);
	}
	template<typename... Args>
	void log_msg(FILE *fp, Level lvl, std::format_string<Args...> fmt, Args&&... args)
	{
		preamble(lvl, fp);
		std::print(fp, fmt, std::forward<Args>(args)...);
		end(fp);

		if(lvl >= error_level and fp != stdout and fp != stderr)
		{
			preamble(lvl, stderr);
			std::print(stderr, fmt, std::forward<Args>(args)...);
			end(stderr);
		}
	}

	// settings
	bool  initialized  { true };
	Level level        { WARNING };
	Level error_level  { WARNING };  // at or higher also writes to stderr
	FILE  *out         { stdout };
	bool  also_stdout  { false }; // when logging to file
	bool  output_date  { false };
	bool  output_since { false };
	std::chrono::steady_clock::time_point start_time;
};

bool set_file(const std::filesystem::path &file_path, bool also_stdout=false);
Level set_level(Level min_level);
void enable_date(bool enable);
void enable_since(bool enable);
void flush();
void close();

template<typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args)
{
	if(_private::the().level <= DEBUG)
		_private::the().log_msg(DEBUG, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args)
{
	if(_private::the().level <= INFO)
		_private::the().log_msg(INFO, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void warning(std::format_string<Args...> fmt, Args&&... args)
{
	if(_private::the().level <= WARNING)
		_private::the().log_msg(WARNING, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args)
{
	if(_private::the().level <= ERROR)
		_private::the().log_msg(ERROR, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void fatal(std::format_string<Args...> fmt, Args&&... args)
{
	if(_private::the().level <= FATAL)
		_private::the().log_msg(FATAL, fmt, std::forward<Args>(args)...);

	Log::close();
	std::exit(EXIT_FAILURE);
}

} // log
