#include "ssbo.h"

ShaderStorageBuffer::ShaderStorageBuffer(GLuint bind_index) :
	_bind_index(bind_index)
{
}

ShaderStorageBuffer::~ShaderStorageBuffer()
{
	if(created())
		glDeleteBuffers(1, &_id);
	_id = GLuint(-1);
}
