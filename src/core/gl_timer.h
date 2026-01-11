#pragma once


#include "glad/glad.h"
#include <chrono>

namespace RGL
{

class GLTimer
{
public:
	void start();

	template<typename D=std::chrono::nanoseconds>
	[[nodiscard]] D elapsed(bool start_new_timer=false) const
	{
		glEndQuery(GL_TIME_ELAPSED);
		GLuint elapsed { 0 };
		glGetQueryObjectuiv(_timer, GL_QUERY_RESULT, &elapsed);
		_started = false;

		const auto t = std::chrono::duration_cast<D>(std::chrono::nanoseconds(elapsed));

		if(start_new_timer)
			const_cast<GLTimer *>(this)->start();

		return t;
	}

	inline bool is_started() const { return _started; }

private:
	GLuint	_timer { 0 };
	mutable bool _started{ false };
};

} // RGL
