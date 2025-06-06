#pragma once

#include "buffer.h"

namespace RGL::buffer
{

template<typename T>
class Uniform : public Buffer
{
public:
	Uniform() : Buffer()
	{
		_buffer_type = GL_UNIFORM_BUFFER;
	}

	inline T *operator -> () { return &_data; }

	void flush();

private:
	T _data;
};

template<typename T>
inline void Uniform<T>::flush()
{
	ensureCreated();

	glNamedBufferData(id(), sizeof(T), &_data, this->usage());
}

} // RGL::buffer
