#include "GLTimer.h"

#include <assert.h>

namespace RGL
{

void GLTimer::start()
{
	if(not _timer)
	{
		glGenQueries(1, &_timer);
		assert(_timer != 0);
	}

	glBeginQuery(GL_TIME_ELAPSED, _timer);
}



} // RGL
