#pragma once

#include "glad/glad.h"

#include <cstring>
#include <memory>
#include <vector>
#include <cassert>
#include <span>


enum BufferUsage : GLenum
{
	DefaultUsage = 0,
	DynamicDraw = GL_DYNAMIC_DRAW,
	StaticRead  = GL_STATIC_READ,
	StaticDraw  = GL_STATIC_DRAW,
};

template<typename T>
class ShaderStorageBuffer
{
public:
	using value_type = T;

public:
	ShaderStorageBuffer(BufferUsage default_usage=DynamicDraw) :
		_bind_index(0),
		_size(0),
		_default_usage(default_usage)
	{
	}
	~ShaderStorageBuffer()
	{
		if(*this)
			glDeleteBuffers(1, &_id);
		_id = 0;
	}

	void setBindIndex(GLuint index);

	inline uint32_t id() const { return _id; }
	void bind();
	void bindIndirect() const;

	void set(const std::vector<T> &data, BufferUsage usage=DefaultUsage);
	bool set(size_t index, const T &item);
	void clear();

	inline size_t size() const { return _size; }
	inline BufferUsage usage() const { return _default_usage; }

	void resize(size_t size);

	void copyTo(ShaderStorageBuffer<T> &dest, size_t count=0, size_t readStart=0, size_t writeStart=0) const;

	inline operator bool () const { return _id >= 0; }

	class View : public std::span<const T, std::dynamic_extent>
	{
		friend class ShaderStorageBuffer;
	public:
		virtual ~View();
	private:
		View(ShaderStorageBuffer<T> *buffer, const T *start);
	private:
		ShaderStorageBuffer<T> *_buffer;
	};

	std::unique_ptr<const typename ShaderStorageBuffer<T>::View> view();

protected:
	bool ensureCreated();  // returns true if it was created
	void releaseView();

private:
	GLuint _id { 0 };
	GLuint _bind_index { 0 };
	BufferUsage _default_usage;
	size_t _size;
	bool _view_active { false };
};


template<typename T>
inline void ShaderStorageBuffer<T>::setBindIndex(GLuint index)
{
	_bind_index = index;
	if(_id > 0)
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, _bind_index, _id);
}

template<typename T>
inline void ShaderStorageBuffer<T>::bind()
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, _id);
}

template<typename T>
inline void ShaderStorageBuffer<T>::bindIndirect() const
{
	glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, _id);
}

template<typename T>
void ShaderStorageBuffer<T>::set(const std::vector<T> &data, BufferUsage usage)
{
	if(data.empty())
	{
		if(_size == 0)
			return;
	}
	else
	{
		ensureCreated();

		glNamedBufferData(_id, data.size() * sizeof(T), data.data(), usage != DefaultUsage? usage: _default_usage);
		_size = data.size();
	}
}

template<typename T>
inline bool ShaderStorageBuffer<T>::set(size_t index, const T &item)
{
	ensureCreated();

	assert(index < _size);
	if(index < _size)
		glNamedBufferSubData(_id, index * sizeof(T), sizeof(T), &item);

	return index < _size;
}

template<typename T>
inline void ShaderStorageBuffer<T>::clear()
{
	ensureCreated();

	static constexpr uint32_t clear_val = 0;
	glClearNamedBufferData(_id, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
}

template<typename T>
inline void ShaderStorageBuffer<T>::resize(size_t size)
{
	ensureCreated();

	assert(size > 0);
	glNamedBufferData(_id, size * sizeof(T), nullptr, _default_usage);
	_size = size;
}

template<typename T>
void ShaderStorageBuffer<T>::copyTo(ShaderStorageBuffer<T> &dest, size_t count, size_t readStart, size_t writeStart) const
{
	ensureCreated();

	if(not count)
		count = _size;

	assert(readStart + count <= _size);
	assert(writeStart + count <= dest.size());

	glCopyNamedBufferSubData(_id, dest._id, GLintptr(readStart*sizeof(T)), GLintptr(writeStart*sizeof(T)), GLsizeiptr(count*sizeof(T)));
}

template<typename T>
bool ShaderStorageBuffer<T>::ensureCreated()
{
	if(_id == 0)
	{
		glCreateBuffers(1, &_id);
		assert(_id > 0);

		if(_bind_index > 0)
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, _bind_index, _id);

		return true;
	}

	return false;
}

template<typename T>
std::unique_ptr<const typename ShaderStorageBuffer<T>::View> ShaderStorageBuffer<T>::view()
{
	assert(_id and _size and not _view_active);
	if(_view_active)
	{
		std::fprintf(stderr, "SSBO: Returning NULL view b/c a view already exists!\n");
		return {};
	}

	// TODO: prevent multiple views
	const T *start = static_cast<const T *>(glMapNamedBuffer(_id, GL_READ_ONLY));
	auto *v = new View(this, start);
	_view_active = true;
	return std::unique_ptr<View>(v);
}

template<typename T>
void ShaderStorageBuffer<T>::releaseView()
{
	glUnmapNamedBuffer(_id);
	_view_active = false;
}

template<typename T>
inline ShaderStorageBuffer<T>::View::View(ShaderStorageBuffer<T> *buffer, const T *start) :
	std::span<const T, std::dynamic_extent>(start, buffer->size()),
	_buffer(buffer)
{
}

template<typename T>
inline ShaderStorageBuffer<T>::View::~View()
{
	_buffer->releaseView();
}

// ============================================================================

template<size_t count=1> requires (count <= 32)
class AtomicCounterBuffer : protected ShaderStorageBuffer<uint32_t>
{
public:
	AtomicCounterBuffer(BufferUsage usage=DefaultUsage);

	void bindCounters();
	void clear();

	template<size_t index> requires (index < count)
	void set(uint32_t value);
};

template<size_t count> requires (count <= 32)
inline AtomicCounterBuffer<count>::AtomicCounterBuffer(BufferUsage usage) :
	ShaderStorageBuffer<uint32_t>(usage)
{
}

template<size_t count> requires (count <= 32)
void AtomicCounterBuffer<count>::bindCounters()
{
	if(ensureCreated())
		resize(count);

	glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, id());
}

template<size_t count> requires (count <= 32)
void AtomicCounterBuffer<count>::clear()
{
	if(ensureCreated())
		resize(count);

	GLuint values[count];
	std::memset(&values, sizeof(uint32_t)*count, 0);

	glNamedBufferData(id(), sizeof(values), values, usage());
}

template<size_t count> requires (count <= 32)
template<size_t index> requires (index < count)
void AtomicCounterBuffer<count>::set(uint32_t value)
{
	if(ensureCreated())
		resize(count);

	glNamedBufferSubData(id(), index + sizeof(uint32_t), sizeof(uint32_t), &value);
}
