#pragma once

#include "glad/glad.h"
#include <vector>


class ShaderStorageBuffer
{
public:
	ShaderStorageBuffer(GLuint bind_index=0);
	~ShaderStorageBuffer();

	template<typename T>
	void set(const std::vector<T> &data, GLenum usage=GL_DYNAMIC_DRAW);

private:
	inline bool created() const { return _id != GLuint(-1); }

private:
	GLuint _id { GLuint(-1) };
	GLuint _bind_index { 0 };
};


template<typename T>
void ShaderStorageBuffer::set(const std::vector<T> &data, GLenum usage)
{
	const auto create = not created();
	if(create)
		glCreateBuffers  (1, &_id);

	glNamedBufferData(_id, GLsizeiptr(sizeof(T) * data.size()), data.data(), usage);

	if(create)
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, _bind_index, _id);
}
