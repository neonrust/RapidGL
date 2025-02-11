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
	D elapsed() const
	{
		glEndQuery(GL_TIME_ELAPSED);
		GLuint elapsed { 0 };
		glGetQueryObjectuiv(_timer, GL_QUERY_RESULT, &elapsed);

		return  std::chrono::duration_cast<D>(std::chrono::nanoseconds(elapsed));
	}

private:
	GLuint	_timer { 0 };
};

} // RGL
