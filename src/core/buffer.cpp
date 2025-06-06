#include "buffer.h"

#include <cassert>

namespace RGL::buffer
{

void Buffer::bind()
{
	glBindBuffer(_buffer_type, _id);
}

void Buffer::setBindIndex(GLuint index)
{
	_bind_index = index;
	if(_id > 0)
		glBindBufferBase(_buffer_type, _bind_index, _id);
}

bool Buffer::ensureCreated() const
{
	if(_id == 0)
	{
		glCreateBuffers(1, &_id);
		assert(_id > 0);

		if(_bind_index != GLuint(-1))
			glBindBufferBase(_buffer_type, _bind_index, _id);

		const_cast<Buffer *>(this)->onCreate();
		return true;
	}

	return false;
}



} // RGL::Buffer
