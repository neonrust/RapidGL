#pragma once

#include "glad/glad.h"
#include <vector>


template<typename T>
class ShaderStorageBuffer
{
public:
	using value_type = T;

public:
	ShaderStorageBuffer(GLuint bind_index=0, GLenum default_usage=GL_DYNAMIC_DRAW) :
		_bind_index(bind_index),
		_default_usage(default_usage)
	{
	}
	~ShaderStorageBuffer()
	{
		if(created())
			glDeleteBuffers(1, &_id);
		_id = GLuint(-1);
	}

	void set(const std::vector<T> &data, GLenum usage=0);

private:
	inline bool created() const { return _id != GLuint(-1); }

private:
	GLuint _id { GLuint(-1) };
	GLuint _bind_index { 0 };
	GLenum _default_usage;
};


template<typename T>
void ShaderStorageBuffer<T>::set(const std::vector<T> &data, GLenum usage)
{
	const auto create = not created();
	if(create)
		glCreateBuffers  (1, &_id);

	glNamedBufferData(_id, GLsizeiptr(sizeof(T) * data.size()), data.data(), usage? usage: _default_usage);

	if(create)
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, _bind_index, _id);
}
