#pragma once

#include "buffer.h"

#include <cstdint>
#include <print>
#include <string_view>
#include <glad/glad.h>

#include <glm/mat4x4.hpp>


namespace RGL
{

template<typename T>
class InstanceAttributes
{
public:
	InstanceAttributes(GLuint binding=1);

	template<typename ItemT>
	uint32_t add(std::string_view name);

	void skip(uint32_t loc, uint32_t offset);

	template<buffer::ContiguousRangeOf<T> R>
	void load(const R &data);
	void bind();

	inline operator bool () const { return _vao > 0; }

private:
	void ensureCreated();

private:
	// dense_map<std::string_view, GLint> _attribute_binds;

	GLuint _buf_bind { 0 };
	GLuint _vao { 0 };
	buffer::Buffer _buf;
	uint32_t _offset { 0 };
	uint32_t _attrib_location { 0 };
};

template<typename T>
InstanceAttributes<T>::InstanceAttributes(GLuint binding) :
	_buf_bind(binding),
	_buf(std::string_view("inst-attrs-buf"))
{
}

template<typename T>
inline void InstanceAttributes<T>::skip(uint32_t loc, uint32_t offset)
{
	_attrib_location += loc;
	assert(_offset + offset <= sizeof(T));
	_offset += offset;
}

template<typename T>
template<buffer::ContiguousRangeOf<T> R>
inline void InstanceAttributes<T>::load(const R &data)
{
	ensureCreated();

	_buf.upload(data.data(), data.size() * sizeof(T));
}

template<typename T>
void InstanceAttributes<T>::bind()
{
	assert(_attrib_location > 0);

	glBindVertexArray(_vao);
}

template<typename T>
void InstanceAttributes<T>::ensureCreated()
{
	if(not _vao)
	{
		_buf.create();

		glCreateVertexArrays(1, &_vao);
		glVertexArrayVertexBuffer(
			_vao,
			_buf_bind,
			_buf.id(),
			0,               // offset
			sizeof(T)
		);
	}
}

template<typename T>
template<typename ItemT>
uint32_t InstanceAttributes<T>::add(std::string_view name)
{
	if constexpr (std::is_same_v<ItemT, glm::mat4>)
	{
		auto loc = add<glm::vec4>(name);
		add<glm::vec4>(name);
		add<glm::vec4>(name);
		add<glm::vec4>(name);

		return loc;
	}
	else
	{
		ensureCreated();

		constexpr auto num_components = std::max<size_t>(1, sizeof(ItemT) / sizeof(float));
		static_assert(num_components >= 1 and num_components <= 4);

		assert(_offset + sizeof(ItemT) <= sizeof(T));

		glEnableVertexArrayAttrib(  _vao, _attrib_location);
		if constexpr (std::is_same_v<ItemT, uint32_t>)
			glVertexArrayAttribIFormat(_vao, _attrib_location, num_components, GL_UNSIGNED_INT,    _offset);
		else
			glVertexArrayAttribFormat( _vao, _attrib_location, num_components, GL_FLOAT, GL_FALSE, _offset);
		glVertexArrayAttribBinding( _vao, _attrib_location, _buf_bind);
		glVertexArrayBindingDivisor(_vao, _buf_bind, 1);

		_offset += sizeof(ItemT);
		return _attrib_location++;
	}
}

} // RGL
