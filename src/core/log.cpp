#include "log.h"

#include <csignal>
#include <cstdio>
#include <chrono>

using namespace std::literals;
using namespace std::chrono;

namespace Log
{

void close()
{
	if(not _private::initialized)
		return;
	_private::initialized = false;

	_private::log_msg(INFO, "Log ended");
	flush();
	if(_private::out != stdout and _private::out != stderr)
		std::fclose(_private::out);
}

void flush()
{
	std::fflush(_private::out);
	if(_private::out != stderr)
		std::fflush(stderr);
}

bool set_file(std::filesystem::path &file_path)
{
	auto *fp = std::fopen(file_path.native().c_str(), "wb");
	if(not fp)
		return false;
	_private::out = fp;
	return true;
}

Level set_level(Level min_level)
{
	const auto old_level = _private::level;
	_private::level = min_level;
	return old_level;
}

void enable_date(bool enable)
{
	_private::output_date = enable;
}

void enable_since(bool enable)
{
	_private::output_since = enable;
}

namespace _private
{

void level_style(Level lvl, FILE *fp)
{
	switch(lvl)
	{
	case DEBUG:   std::print(fp, "\x1b[2m"sv);       break;
	case INFO:                                       break;
	case WARNING: std::print(fp, "\x1b[33;1m"sv);    break;
	case ERROR:   std::print(fp, "\x1b[31;1m"sv);    break;
	case FATAL:   std::print(fp, "\x1b[31;97;1m"sv); break;
	}
}

void reset_style(FILE *fp)
{
	std::print(fp, "\x1b[m");
}

void out_level(Level lvl, FILE *fp)
{
	switch(lvl)
	{
	case DEBUG:   std::print(fp, "DEBUG "sv); break;
	case INFO:    std::print(fp, "INFO  "sv); break;
	case WARNING: std::print(fp, "\x1b[33;1mWARN\x1b[m  "sv); break;
	case ERROR:   std::print(fp, "\x1b[31;1mERROR\x1b[m "sv); break;
	case FATAL:   std::print(fp, "\x1b[41;97;1mFATAL\x1b[m "sv); break;
	}
}

void stamp(FILE *fp)
{
	const auto now = time_point_cast<milliseconds>(system_clock::now());
	if(output_date)
		std::print(fp, "\x1b[34;1m{:%Y-%m-%d %H:%M:%S}\x1b[30m ", now);
	else
		std::print(fp, "\x1b[34;1m{:%H:%M:%S}\x1b[30m ", now);
	if(output_since)
		std::print(fp, "({:.3f}) ", duration_cast<seconds_f>(now - start_time).count());
}

void lf(FILE *fp)
{
	std::fputc('\n', fp);
}

void preamble(Level lvl, FILE *fp)
{
	stamp(fp);
	out_level(lvl, fp);
	level_style(lvl, fp);

	// TODO: other columns
}

void end(FILE *fp)
{
	// TODO: other stuff? e.g. currently added context

	reset_style(fp);
	lf(fp);
}

void on_signal(int signum)
{
	Log::warning("Received signal {}  (flushing)", signum);
	Log::flush();
}

void init()
{
	::atexit(Log::close);
	std::signal(SIGINT,   _private::on_signal);
	std::signal(SIGTERM,  _private::on_signal);
	std::signal(SIGABRT,  _private::on_signal);
	std::signal(SIGFPE,   _private::on_signal);
	std::signal(SIGWINCH, _private::on_signal);
	initialized = true;
	start_time = time_point_cast<milliseconds>(system_clock::now());
}

struct _static_init
{
	_static_init() {
		init();
	}
};
static const _static_init _initializer_;

}  // _private

} // log
