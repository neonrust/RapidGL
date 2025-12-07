#pragma once

#include "buffer.h"

#include <cstdint>
#include <print>
#include <string_view>
#include <glad/glad.h>

#include <glm/mat4x4.hpp>


namespace RGL
{

class InstanceAttributes
{
public:
	InstanceAttributes();
	~InstanceAttributes();

	void config(size_t stride, GLuint bindingIndex=5);
	void config_with_vao(GLuint vao, size_t stride, GLuint bindingIndex=5);

	template<typename ItemT>
	uint32_t add(std::string_view name);

	void skip(uint32_t loc=1, uint32_t offset=0);

	template<typename T, buffer::ContiguousRangeOf<T> R>
	void load(const R &data);
	void bind_vao();

	inline operator bool () const { return _buf and _vao > 0 and _stride > 0 and _attrib_location > 0; }

private:
	template<typename T, size_t N> requires (N >= 1 and N <= 4)
	uint32_t _add(std::string_view name);
	void ensureCreated();

private:
	GLsizei _stride { 0 };
	GLuint _bind_index { 0 };
	bool _vao_owner { true }; // whether '_vao' is created by us
	GLuint _vao { 0 };
	buffer::Buffer _buf;
	uint32_t _offset { 0 };
	uint32_t _attrib_location { 0 };
};

template<typename T, buffer::ContiguousRangeOf<T> R>
inline void InstanceAttributes::load(const R &data)
{
	ensureCreated();

	_buf.upload(data.data(), data.size() * sizeof(T));
}

namespace _private
{

template<typename T>
struct glm_vec_traits
{
	static constexpr bool is_vec = false;
};

template<glm::length_t N, typename T, glm::qualifier Q>
struct glm_vec_traits<glm::vec<N, T, Q>>
{
	static constexpr bool is_vec = true;
	static constexpr glm::length_t length = N;
	using value_type = T;
	static constexpr glm::qualifier qualifier = Q;
};

template<typename T>
struct glm_mat_traits
{
	static constexpr bool is_mat = false;
};

template<glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
struct glm_mat_traits<glm::mat<C, R, T, Q>>
{
	static constexpr bool is_mat = true;
	static constexpr glm::length_t columns = C;
	static constexpr glm::length_t rows = R;
	using value_type = T;
	static constexpr glm::qualifier qualifier = Q;
};

} // _private


template<typename T>
uint32_t InstanceAttributes::add(std::string_view name)
{
	using vec_traits = typename _private::glm_vec_traits<T>;
	using mat_traits = typename _private::glm_mat_traits<T>;

	if constexpr (mat_traits::is_mat)
	{
		static_assert(std::is_same_v<typename mat_traits::value_type, float>);
		static constexpr auto C = mat_traits::columns;
		static constexpr auto R = mat_traits::rows;

		const auto loc = add<glm::vec<R, float, mat_traits::qualifier>>(name);
		add<glm::vec<R, float, mat_traits::qualifier>>(name);
		if constexpr (C >= 3)
			add<glm::vec<R, float, mat_traits::qualifier>>(name);
		if constexpr (C >= 4)
			add<glm::vec<R, float, mat_traits::qualifier>>(name);
		return loc;
	}
	else if constexpr (vec_traits::is_vec)
	{
		return _add<typename vec_traits::value_type, vec_traits::length>(name);
	}
	else
		return _add<T, 1>(name);
}

template<typename T, size_t N> requires (N >= 1 and N <= 4)
uint32_t InstanceAttributes::_add(std::string_view name)
{
	ensureCreated();

	assert(_offset + sizeof(T)*N <= _stride);

	const auto loc = _attrib_location++;

	bind_vao();
	_buf.bindCurrent();  // bind buffer to associate it to the attribute (association stored in VAO)
	glEnableVertexArrayAttrib(_vao, loc);

	std::print(" inst attr[{}] {:<14} @ {:<2}  size:{:>2}; {}x", loc, name, _offset, sizeof(T)*N, N);
	if constexpr (std::is_same_v<T, float>)
	{
		// T assumed to be float or vecN
		glVertexArrayAttribFormat( _vao, loc, N, GL_FLOAT, GL_FALSE, _offset);
		std::print(" float\n");
	}
	else if constexpr (std::is_same_v<T, uint32_t>)
	{
		glVertexArrayAttribIFormat(_vao, loc, N, GL_UNSIGNED_INT,    _offset);
		std::print(" uint\n");
	}
	else if constexpr (std::is_same_v<T, double>)
	{
		// T assumed to be float or vecN
		glVertexArrayAttribLFormat( _vao, loc, N, GL_DOUBLE,         _offset);
		std::print(" double\n");
	}
	else
		static_assert(false); // unsupported component type

	glVertexArrayAttribBinding( _vao, loc, _bind_index);
	glVertexArrayBindingDivisor(_vao, _bind_index, 1);

	_offset += sizeof(T)*N;
	return loc;
}

} // RGL
