#pragma once

#include "buffer.h"
#include <cstring>


namespace RGL::buffer
{

template<typename T>  // must be @ubo struct
class Uniform : public Buffer
{
public:
	Uniform(std::string_view name) :
		Buffer(name, GL_UNIFORM_BUFFER)
	{
	}

	inline T *operator -> () {return &_data; }

	void clear();
	void flush();

private:
	T _data;
};

template<typename T>
inline void Uniform<T>::clear()
{
	std::memset(&_data, 0, sizeof(_data));
}

template<typename T>
inline void Uniform<T>::flush()
{
	ensureCreated();

	// TODO: keep track of changes, to avoid needless uploads?
	//   that would require double the storage though, in the current design.

	upload(&_data, sizeof(_data));
}

} // RGL::buffer
