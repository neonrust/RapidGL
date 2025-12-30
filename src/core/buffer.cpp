#include "buffer.h"
#include "gl_lookup.h"
#include "log.h"

#include <cassert>
#include <print>

namespace RGL::buffer
{

Buffer::~Buffer()
{
	if(_id)
	{
		glDeleteBuffers(1, &_id);
		Log::debug("Buffer[{}]: deleted {} ({})", _name, gl_lookup::enum_name(_buffer_type), _id);
		_id = 0;
	}
}

bool Buffer::create()
{
	glCreateBuffers(1, &_id);
	assert(_id > 0);
	Log::debug("Buffer[{}]: created {} -> {}", _name, gl_lookup::enum_name(_buffer_type), _id);

	if(_bind_index != GLuint(-1))
		glBindBufferBase(_buffer_type, _bind_index, _id);

	onCreate();
	return true;
}

void Buffer::bindCurrent()
{
	ensureCreated();

	glBindBuffer(_buffer_type, _id);
}

void Buffer::bindAt(GLuint index)
{
	_bind_index = index;
	if(_id)
		glBindBufferBase(_buffer_type, _bind_index, _id);
}

bool Buffer::ensureCreated() const
{
	if(_id == 0)
		return const_cast<Buffer *>(this)->create();
	return false;
}

void Buffer::clear()
{
	ensureCreated();

	static constexpr uint32_t clear_val = 0;
	glClearNamedBufferData(id(), GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
}

void Buffer::upload(const void *ptr, size_t size)
{
	if(_default_usage == BufferUsage::ReadBack)
	{
		assert(ptr == nullptr);  // not an actual upload
		glNamedBufferStorage(id(), GLsizeiptr(size), nullptr, GL_MAP_READ_BIT | GL_DYNAMIC_STORAGE_BIT);
		// Log::debug("Buffer[{}]: uploaded {} bytes GL_MAP_READ_BIT | GL_DYNAMIC_STORAGE_BIT", _name, size);
	}
	else
	{
		glNamedBufferData(id(),  GLsizeiptr(size), ptr, GLenum(_default_usage));
		// Log::debug("Buffer[{}]: uploaded {} bytes {}", _name, size, gl_lookup::enum_name(GLenum(usage())));
	}
}

void Buffer::upload(const void *ptr, size_t size, size_t start_offset)
{
	assert(_default_usage != BufferUsage::ReadBack);

	glNamedBufferSubData(id(), GLintptr(start_offset), GLsizeiptr(size), ptr);
	// Log::debug("Buffer[{}]: uploaded {} bytes @ offset {}", _name, size, start_offset);
}

void Buffer::download(void *ptr, size_t size, size_t start_offset)
{
	assert(_default_usage == BufferUsage::ReadBack);
	glGetNamedBufferSubData(id(), GLintptr(start_offset), GLsizeiptr(size), ptr);
	// Log::debug("Buffer[{}]: downloaded {} bytes @ offset {}", _name, size, start_offset);
}



} // RGL::Buffer
