#pragma once

#include "glad/glad.h"

#include <string_view>

namespace RGL::buffer
{

template<typename Iter, typename T>
concept IteratorOf = std::contiguous_iterator<Iter> &&
	std::same_as<std::remove_cvref_t<decltype(*std::declval<Iter>())>, T>;

template<typename R, typename T>
concept ContiguousRangeOf =
	std::ranges::contiguous_range<R> &&
	std::same_as<std::ranges::range_value_t<R>, T>;


enum class BufferUsage : GLenum
{
	DefaultUsage = 0,
	DynamicDraw = GL_DYNAMIC_DRAW,
	StaticRead  = GL_STATIC_READ,
	StaticDraw  = GL_STATIC_DRAW,
	ReadBack    = GL_MAP_READ_BIT,
};


class Buffer
{
public:
	inline Buffer(std::string_view name, GLenum buffer_type=GL_ARRAY_BUFFER, BufferUsage default_usage=BufferUsage::DynamicDraw) :
		_buffer_type(buffer_type),
		_default_usage(default_usage),
		_name(name)
	{
	}

	virtual ~Buffer();

	bool create();
	
	void bindAt(GLuint index);
	void bindCurrent();

	inline uint32_t id() const { return _id; }
	inline BufferUsage usage() const { return _default_usage; }

	void clear();

	inline operator bool () const { return _id > 0; }

	void upload(const void *ptr, size_t size);
	void upload(const void *ptr, size_t size, size_t start_offset);
	void download(void *ptr, size_t size, size_t start_offset=0);

protected:
	bool ensureCreated() const;    // returns true if it was created
	virtual void onCreate() {};

protected:
	GLenum _buffer_type;

private:
	GLuint _id { 0 };
	BufferUsage _default_usage;
	std::string_view _name;
	GLuint _bind_index { GLuint(-1) };
};

} // RGL::buffer
