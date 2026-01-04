#include "log.h"

#include <csignal>
#include <cstdio>
#include <chrono>

using namespace std::literals;
using namespace std::chrono;
namespace fs = std::filesystem;

namespace Log
{

void close()
{
	if(not _private::the().initialized)
		return;
	_private::the().initialized = false;

	_private::the().log_msg(INFO, "Log ended");
	flush();
	if(_private::the().out != stdout and _private::the().out != stderr)
		std::fclose(_private::the().out);
}

void flush()
{
	std::fflush(_private::the().out);
	if(_private::the().out != stderr)
		std::fflush(stderr);
}

bool set_file(fs::path &file_path)
{
	auto *fp = std::fopen(file_path.native().c_str(), "wb");
	if(not fp)
		return false;
	_private::the().out = fp;
	return true;
}

Level set_level(Level min_level)
{
	const auto old_level = _private::the().level;
	_private::the().level = min_level;
	return old_level;
}

void enable_date(bool enable)
{
	_private::the().output_date = enable;
}

void enable_since(bool enable)
{
	_private::the().output_since = enable;
}

void _private::level_style(Level lvl, FILE *fp)
{
	switch(lvl)
	{
	case DEBUG:   std::fputs("\x1b[2m", fp);       break;
	case INFO:    std::fputs("\x1b[m", fp);        break;
	case WARNING: std::fputs("\x1b[m", fp);        break;
	case ERROR:   std::fputs("\x1b[31;1m", fp);    break;
	case FATAL:   std::fputs("\x1b[31;97;1m", fp); break;
	}

}

void _private::reset_style(FILE *fp)
{
	std::print(fp, "\x1b[m");
}

void _private::out_level(Level lvl, FILE *fp)
{
	switch(lvl)
	{
	case DEBUG:   std::fputs("\x1b[mDEBUG ", fp); break;
	case INFO:    std::fputs("\x1b[mINFO  ", fp); break;
	case WARNING: std::fputs("\x1b[33;1mWARN  ", fp); break;
	case ERROR:   std::fputs("\x1b[31;1mERROR ", fp); break;
	case FATAL:   std::fputs("\x1b[41;97;1mFATAL ", fp); break;
	}
}

void _private::stamp(FILE *fp)
{
	const auto now = time_point_cast<milliseconds>(system_clock::now());
	if(output_date)
		std::print(fp, "\x1b[34;1m{:%Y-%m-%d %H:%M:%S} ", now);
	else
		std::print(fp, "\x1b[34;1m{:%H:%M:%S} ", now);
	if(output_since)
	{
		const auto since = duration_cast<seconds_f>(steady_clock::now() - start_time);
		std::print(fp, "\x1b[32;1m{:.3f} ", since.count());
	}
}

void _private::lf(FILE *fp)
{
	std::fputc('\n', fp);
}

void _private::preamble(Level lvl, FILE *fp)
{
	stamp(fp);
	out_level(lvl, fp);
	level_style(lvl, fp);

	// TODO: other columns
}

void _private::end(FILE *fp)
{
	// TODO: other stuff? e.g. currently added context

	reset_style(fp);
	lf(fp);
}

void _private::on_signal(int signum)
{
	static std::string_view signal_name[] = {
		{},
		"SIGHUP",    // 1
		"SIGINT",    // 2
		"SIGQUIT",   // 3
		"SIGILL",    // 4
		"SIGTRAP",   // 5
		"SIGABRT",   // 6
		"SIGBUS",    // 7
		"SIGFPE",    // 8
		"SIGKILL",   // 9
		"SIGUSR1",   // 10
		"SIGSEGV",   // 11
		"SIGUSR2",   // 12
		"SIGPIPE",   // 13
		"SIGALRM",   // 14
		"SIGTERM",   // 15
		"SIGSTKFLT", // 16
		"SIGCHLD",   // 17
		"SIGCONT",   // 18
		"SIGSTOP",   // 19
		"SIGTSTP",   // 20
		"SIGTTIN",   // 21
		"SIGTTOU",   // 22
		"SIGURG",    // 23
		"SIGXCPU",   // 24
		"SIGXFSZ",   // 25
		"SIGVTALRM", // 26
		"SIGPROF",   // 27
		"SIGWINCH",  // 28
		"SIGLOST",   // 29
		"SIGPWR",    // 30
		"SIGSYS",    // 31
		"SIGRTMIN"   // 32
	};
	if(signum > 0 and signum < int(std::size(signal_name)))
		Log::warning("Received {} ({})  (flushing)", signal_name[signum], signum);
	else
		Log::warning("Received unknown signal {}  (flushing)", signum);

	Log::flush();
}

_private::_private()
{
	::atexit(Log::close);
	std::signal(SIGINT,   on_signal);
	std::signal(SIGTERM,  on_signal);
	std::signal(SIGABRT,  on_signal);
	std::signal(SIGFPE,   on_signal);
	std::signal(SIGWINCH, on_signal);
	initialized = true;
	start_time = time_point_cast<milliseconds>(steady_clock::now());
}

// static struct _static_init
// {
// 	_static_init() {
// 		_private::the();
// 	}
// } _initializer_;

} // log
