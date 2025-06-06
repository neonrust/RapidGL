#pragma once

#include "glad/glad.h"


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
	Buffer(BufferUsage default_usage=DynamicDraw) :
		_default_usage(default_usage)
	{
	}

	~Buffer()
	{
		if(*this)
			glDeleteBuffers(1, &_id);
		_id = 0;
	}


	void setBindIndex(GLuint index);
	void bind();

	inline uint32_t id() const { return _id; }
	inline BufferUsage usage() const { return _default_usage; }

	inline operator bool () const { return _id >= 0; }


protected:
	bool ensureCreated() const;    // returns true if it was created
	virtual void onCreate() {};

protected:
	GLenum _buffer_type;

private:
	mutable GLuint _id { 0 };
	GLuint _bind_index { GLuint(-1) };
	BufferUsage _default_usage;
};

} // RGL::buffer
