#pragma once

#include "glad/glad.h"
#include <cassert>
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
	size_t size() const { return _size; }

	bool set(size_t index, const T &item);

	void copyTo(ShaderStorageBuffer<T> &dest, size_t count=0, size_t readStart=0, size_t writeStart=0) const;

private:
	inline bool created() const { return _id != GLuint(-1); }

private:
	GLuint _id { GLuint(-1) };
	GLuint _bind_index { 0 };
	GLenum _default_usage;
	size_t _size;
};


template<typename T>
void ShaderStorageBuffer<T>::set(const std::vector<T> &data, GLenum usage)
{
	const auto create = not created();
	if(create)
		glCreateBuffers  (1, &_id);

	glNamedBufferData(_id, GLsizeiptr(sizeof(T) * data.size()), data.data(), usage? usage: _default_usage);
	_size = data.size();

	// TODO: should use glNamedBufferSubData for updates
	//   if so, the array data needs to have a "terminator" (so shader code can tell the size)
	// glNamedBufferSubData(_id, 0,  GLsizeiptr(sizeof(T) * data.size()), data.data());

	if(create)
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, _bind_index, _id);
}

template<typename T>
inline bool ShaderStorageBuffer<T>::set(size_t index, const T &item)
{
	if(index < _size)
		glNamedBufferSubData(_id, index*sizeof(T), sizeof(T), &item);

	return index < _size;
}

template<typename T>
inline void ShaderStorageBuffer<T>::copyTo(ShaderStorageBuffer<T> &dest, size_t count, size_t readStart, size_t writeStart) const
{
	assert(readStart + count < _size);
	assert(writeStart + count < dest.size());

	glCopyNamedBufferSubData(_id, dest._id, GLintptr(readStart*sizeof(T)), GLintptr(writeStart*sizeof(T)), GLsizeiptr(count*sizeof(T)));
}
