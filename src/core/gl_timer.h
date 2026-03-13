#pragma once

#include "glad/glad.h"
#include <chrono>
#include <print>

namespace RGL
{

template<size_t frame_latency=4>
class GLTimer
{
public:
	inline void start()
	{
		if(_frame == std::numeric_limits<size_t>::max())
		{
			glCreateQueries(GL_TIME_ELAPSED, frame_latency, _timer.data());
			_frame = 0;
		}

		++_frame;
		glBeginQuery(GL_TIME_ELAPSED, _timer[_frame % frame_latency]);
	}

	template<typename D=std::chrono::nanoseconds>
	[[nodiscard]] std::optional<D> elapsed() const
	{
		auto ns = _try_elapsed();
		if(not ns)
			return std::nullopt;

		return std::chrono::duration_cast<D>(std::chrono::nanoseconds(ns));
	}

private:
	GLuint64 _try_elapsed() const
	{
		glEndQuery(GL_TIME_ELAPSED);

		if(_frame < frame_latency) // ring buffer isn't full yet
			return 0;

		// read the oldest timer
		const auto read_timer = (_frame + 1) % frame_latency;

		GLuint available = 0;
		glGetQueryObjectuiv(_timer[read_timer], GL_QUERY_RESULT_AVAILABLE, &available);
		if(not available)
		{
			std::println(stderr, "timer {} not available", read_timer);
			return 0;
		}

		GLuint64 elapsed_ns { 0 };
		glGetQueryObjectui64v(_timer[read_timer], GL_QUERY_RESULT, &elapsed_ns);

		return elapsed_ns;
	}

private:
	std::array <GLuint, frame_latency> _timer;
	mutable size_t _frame { std::numeric_limits<size_t>::max() };
};

} // RGL
