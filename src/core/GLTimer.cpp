#include "GLTimer.h"

#include <cassert>

namespace RGL
{

void GLTimer::start()
{
	if(not _timer)
	{
		glGenQueries(1, &_timer);
		assert(_timer != 0);
	}
	else if(_started)
		std::ignore = elapsed(false);

	glBeginQuery(GL_TIME_ELAPSED, _timer);
	_started = true;
}



} // RGL
