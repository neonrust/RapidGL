#include "buffer.h"
#include "gl_lookup.h"

#include <cassert>
#include <print>

namespace RGL::buffer
{

Buffer::~Buffer()
{
	if(_id)
	{
		glDeleteBuffers(1, &_id);
		std::print("Buffer[{}]: deleted {} ({})\n", _name, gl_lookup::enum_name(_buffer_type), _id);
		_id = 0;
	}
}

void Buffer::bind()
{
	ensureCreated();

	glBindBuffer(_buffer_type, _id);
}

void Buffer::clear()
{
	ensureCreated();

	static constexpr uint32_t clear_val = 0;
	glClearNamedBufferData(id(), GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
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
		std::print("Buffer[{}]: created {} -> {}\n", _name, gl_lookup::enum_name(_buffer_type), _id);

		if(_bind_index != GLuint(-1))
			glBindBufferBase(_buffer_type, _bind_index, _id);

		const_cast<Buffer *>(this)->onCreate();
		return true;
	}

	return false;
}

void Buffer::upload(const void *ptr, size_t size)
{
	glNamedBufferData(id(),  GLsizeiptr(size), ptr, _default_usage);
	// std::print("Buffer[{}]: uploaded {} bytes\n", _name, size);
}

void Buffer::upload(const void *ptr, size_t size, size_t start_offset)
{
	glNamedBufferSubData(id(), GLintptr(start_offset), GLsizeiptr(size), ptr);
	// std::print("Buffer[{}]: uploaded {} bytes @ offset {}\n", _name, size, start_offset);
}



} // RGL::Buffer
