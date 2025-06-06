#pragma once

#include "glad/glad.h"

#include <cstring>
#include <memory>
#include <vector>
#include <cassert>
#include <span>
#include <print>

#include "buffer.h"

namespace RGL::buffer
{

template<typename T>
class ShaderStorage : public Buffer
{
public:
	using value_type = T;

public:
	ShaderStorage(BufferUsage default_usage=DynamicDraw) :
		Buffer(default_usage)
	{
		_buffer_type = GL_SHADER_STORAGE_BUFFER;
	}

	void bindIndirect() const;

	void set(const std::vector<T> &data, BufferUsage usage=DefaultUsage);
	bool set(size_t index, const T &item);
	void clear();

	inline size_t size() const { return _size; }

	void resize(size_t size);

	void copyTo(ShaderStorage<T> &dest, size_t count=0, size_t readStart=0, size_t writeStart=0) const;


	class View : public std::span<const T, std::dynamic_extent>
	{
		friend class ShaderStorage;
	public:
		virtual ~View();
	private:
		View(ShaderStorage<T> *buffer, const T *start);
	private:
		ShaderStorage<T> *_buffer;
	};

	std::unique_ptr<const typename ShaderStorage<T>::View> view();

protected:
	void releaseView();

private:
	size_t _size { 0 };
	bool _view_active { false };
};


template<typename T>
inline void ShaderStorage<T>::bindIndirect() const
{
	glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, id());
}

template<typename T>
void ShaderStorage<T>::set(const std::vector<T> &data, BufferUsage usage)
{
	ensureCreated();

	glNamedBufferData(id(), data.size() * sizeof(T), data.data(), usage != DefaultUsage? usage: this->usage());
	_size = data.size();
}

template<typename T>
inline bool ShaderStorage<T>::set(size_t index, const T &item)
{
	ensureCreated();

	assert(index < _size);
	if(index < _size)
		glNamedBufferSubData(id(), index * sizeof(T), sizeof(T), &item);

	return index < _size;
}

template<typename T>
inline void ShaderStorage<T>::clear()
{
	ensureCreated();

	static constexpr uint32_t clear_val = 0;
	glClearNamedBufferData(id(), GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
}

template<typename T>
inline void ShaderStorage<T>::resize(size_t size)
{
	ensureCreated();

	assert(size > 0);
	glNamedBufferData(id(), size * sizeof(T), nullptr, usage());
	_size = size;
}

template<typename T>
void ShaderStorage<T>::copyTo(ShaderStorage<T> &dest, size_t count, size_t readStart, size_t writeStart) const
{
	ensureCreated();

	if(not count)
		count = _size;

	assert(readStart + count <= _size);
	assert(writeStart + count <= dest.size());

	glCopyNamedBufferSubData(id(), dest.id(), GLintptr(readStart*sizeof(T)), GLintptr(writeStart*sizeof(T)), GLsizeiptr(count*sizeof(T)));
}

template<typename T>
std::unique_ptr<const typename ShaderStorage<T>::View> ShaderStorage<T>::view()
{
	assert(id() and _size and not _view_active);
	if(_view_active)
	{
		std::fprintf(stderr, "SSBO: Returning NULL view b/c a view already exists!\n");
		return {};
	}

	// TODO: prevent multiple views
	assert(not _view_active);
	const T *start = static_cast<const T *>(glMapNamedBuffer(id(), GL_READ_ONLY));
	auto *v = new View(this, start);
	_view_active = true;
	return std::unique_ptr<View>(v);
}

template<typename T>
void ShaderStorage<T>::releaseView()
{
	assert(_view_active);
	glUnmapNamedBuffer(id());
	_view_active = false;
}

template<typename T>
inline ShaderStorage<T>::View::View(ShaderStorage<T> *buffer, const T *start) :
	std::span<const T, std::dynamic_extent>(start, buffer->size()),
	_buffer(buffer)
{
}

template<typename T>
inline ShaderStorage<T>::View::~View()
{
	_buffer->releaseView();
}

// ============================================================================
// ============================================================================

template<size_t count=1> requires (count <= 32)
class AtomicCounterBuffer : protected ShaderStorage<uint32_t>
{
public:
	AtomicCounterBuffer(BufferUsage usage=DefaultUsage);

	void bindCounters();
	void clear();

	template<size_t index> requires (index < count)
	void set(uint32_t value);

protected:
	void onCreate() override;
};

template<size_t count> requires (count <= 32)
inline AtomicCounterBuffer<count>::AtomicCounterBuffer(BufferUsage usage) :
	ShaderStorage<uint32_t>(usage)
{
	_buffer_type = GL_ATOMIC_COUNTER_BUFFER;
}

template<size_t count> requires (count <= 32)
void AtomicCounterBuffer<count>::bindCounters()
{
	ensureCreated();

	glBindBuffer(_buffer_type, id());
}

template<size_t count> requires (count <= 32)
void AtomicCounterBuffer<count>::clear()
{
	ensureCreated();

	GLuint values[count];
	std::memset(&values, sizeof(uint32_t)*count, 0);

	glNamedBufferData(id(), sizeof(values), values, usage());
}

template<size_t count> requires (count <= 32)
inline void AtomicCounterBuffer<count>::onCreate()
{
	resize(count);
}

template<size_t count> requires (count <= 32)
template<size_t index> requires (index < count)
void AtomicCounterBuffer<count>::set(uint32_t value)
{
	ensureCreated();

	glNamedBufferSubData(id(), index + sizeof(uint32_t), sizeof(uint32_t), &value);
}


// ============================================================================
// ============================================================================


template<typename T, size_t size> requires (size > 0 && sizeof(T) >= 4)
class MappedSSBO : protected ShaderStorage<T>
{
public:
	// TODO: add control over GL_MAP_COHERENT_BIT / GL_MAP_FLUSH_EXPLICIT_BIT use?
	inline MappedSSBO(BufferUsage default_usage=DynamicDraw) : ShaderStorage<T>(default_usage) {};

	using ShaderStorage<T>::setBindIndex;
	using ShaderStorage<T>::clear;

	inline       T *operator -> ()       requires (size == 1) { this->ensureCreated(); return _data; }
	inline const T *operator -> () const requires (size == 1) { this->ensureCreated(); return _data; }

	inline       T &operator [] (size_t index)       requires (size > 1) { this->ensureCreated(); return _data[index]; }
	inline const T &operator [] (size_t index) const requires (size > 1) { this->ensureCreated(); return _data[index]; }

	template<size_t index>
	inline       T &get()       requires (size > 1 && index < size) { this->ensureCreated(); return _data[index]; }
	template<size_t index>
	inline const T &get() const requires (size > 1 && index < size) { this->ensureCreated(); return _data[index]; }

	//! ensure modifications are visible to GPU
	void flush();

	inline T* begin() { this->ensureCreated(); return _data; }
	inline T* end()   { this->ensureCreated(); return _data + size; }

private:
	void onCreate() override;

private:
	T *_data { nullptr };
};

template<typename T, size_t size> requires (size > 0 && sizeof(T) >= 4)
void MappedSSBO<T, size>::flush()
{
	if(not this->id())  // calling this while still unmapped makes little sense
		return;
	glFlushMappedNamedBufferRange(this->id(), 0, size*sizeof(T));
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

template<typename T, size_t size> requires (size > 0 && sizeof(T) >= 4)
inline void MappedSSBO<T, size>::onCreate()
{
	static constexpr auto flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT;
	static constexpr auto map_flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_FLUSH_EXPLICIT_BIT;

	glNamedBufferStorage(this->id(), size*sizeof(T), nullptr, flags);
	_data = static_cast<T *>(glMapNamedBufferRange(this->id(), 0, size*sizeof(T), map_flags));
}

} // RGL::buffer
