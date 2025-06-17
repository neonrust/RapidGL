#pragma once

#include "glad/glad.h"

#include <string_view>

namespace RGL::buffer
{


enum BufferUsage : GLenum
{
	DefaultUsage = 0,
	DynamicDraw = GL_DYNAMIC_DRAW,
	StaticRead  = GL_STATIC_READ,
	StaticDraw  = GL_STATIC_DRAW,
};


class Buffer
{
public:
	inline Buffer(std::string_view name, GLenum buffer_type, BufferUsage default_usage=DynamicDraw) :
		_buffer_type(buffer_type),
		_default_usage(default_usage),
		_name(name)
	{
	}

	virtual ~Buffer();

	void setBindIndex(GLuint index);
	void bind();

	inline uint32_t id() const { return _id; }
	inline BufferUsage usage() const { return _default_usage; }

	void clear();

	inline operator bool () const { return _id >= 0; }


protected:
	bool ensureCreated() const;    // returns true if it was created
	void upload(const void *ptr, size_t size);
	void upload(const void *ptr, size_t size, size_t offset);
	virtual void onCreate() {};

protected:
	GLenum _buffer_type;

private:
	mutable GLuint _id { 0 };
	BufferUsage _default_usage;
	std::string_view _name;
	GLuint _bind_index { GLuint(-1) };
};

} // RGL::buffer
