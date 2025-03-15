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
	D elapsed(bool start_new_timer=false) const
	{
		glEndQuery(GL_TIME_ELAPSED);
		GLuint elapsed { 0 };
		glGetQueryObjectuiv(_timer, GL_QUERY_RESULT, &elapsed);

		const auto t = std::chrono::duration_cast<D>(std::chrono::nanoseconds(elapsed));

		if(start_new_timer)
			const_cast<GLTimer *>(this)->start();

		return t;
	}

private:
	GLuint	_timer { 0 };
};

} // RGL
