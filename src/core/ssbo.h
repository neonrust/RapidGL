#pragma once

#include "glad/glad.h"

#include <memory>
#include <vector>
#include <cassert>
#include <span>
#include <print>
#include <cstring>  // std::memset

#include "buffer.h"

namespace RGL::buffer
{

template<typename T>
class ShaderStorage : public Buffer
{
public:
	using value_type = T;

private:
	static constexpr size_t type_size();
public:
	static constexpr size_t elem_size = type_size();

public:
	ShaderStorage(std::string_view name, BufferUsage default_usage=DynamicDraw) :
		Buffer(name, GL_SHADER_STORAGE_BUFFER, default_usage)
	{
	}

	void bindIndirect() const;

	void set(const std::vector<T> &data);
	bool set(size_t index, const T &item);

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


template<typename T> // requires (std::is_same_v<T, uint32_t>)
void ShaderStorage<T>::bindIndirect() const
{
	glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, id());
}

template<typename T>
void ShaderStorage<T>::set(const std::vector<T> &data)
{
	ensureCreated();

	upload(data.data(), data.size() * elem_size);
	_size = data.size();
}

template<typename T>
bool ShaderStorage<T>::set(size_t index, const T &item)
{
	ensureCreated();

	assert(_size > 0);
	assert(index < _size);
	if(index < _size)
		upload(&item, sizeof(T), index * elem_size);

	return index < _size;
}

template<typename T>
void ShaderStorage<T>::resize(size_t size)
{
	ensureCreated();

	assert(size > 0);

	if(size != _size)
	{
		upload(nullptr, size * elem_size);
		_size = size;
	}
}

template<typename T>
void ShaderStorage<T>::copyTo(ShaderStorage<T> &dest, size_t count, size_t readStart, size_t writeStart) const
{
	// TODO: move to base class

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
		std::print(stderr, "SSBO: Returning NULL view b/c a view already exists!\n");
		return {};
	}

	// TODO: prevent multiple views
	assert(not _view_active);
	const T *start = static_cast<const T *>(glMapNamedBuffer(id(), GL_READ_ONLY));
	auto *v = new View(this, start);
	_view_active = true;
	return std::unique_ptr<View>(v);
}

namespace _private
{
template<typename T>
concept HasSize = requires {
	{ T::_struct_size } -> std::convertible_to<size_t>;
};
}

template<typename T>
constexpr size_t ShaderStorage<T>::type_size()
{
	if constexpr (_private::HasSize<T>)
		return T::_struct_size;
	else
		return sizeof(T);
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
ShaderStorage<T>::View::~View()
{
	_buffer->releaseView();
}

// ============================================================================
// ============================================================================

template<size_t count=1> requires (count <= 32)
class AtomicCounterBuffer : protected ShaderStorage<uint32_t>
{
public:
	AtomicCounterBuffer(std::string_view name, BufferUsage usage=DefaultUsage);

	void clear();

	template<size_t index> requires (index < count)
	void set(uint32_t value);

protected:
	void onCreate() override;
};

template<size_t count> requires (count <= 32)
inline AtomicCounterBuffer<count>::AtomicCounterBuffer(std::string_view name, BufferUsage usage) :
	ShaderStorage<uint32_t>(name, usage)
{
	// TODO: set_buffer_type ?
	_buffer_type = GL_ATOMIC_COUNTER_BUFFER;
}

template<size_t count> requires (count <= 32)
void AtomicCounterBuffer<count>::clear()
{
	ensureCreated();

	GLuint values[count];
	std::memset(&values, sizeof(uint32_t)*count, 0);

	upload(values, sizeof(values));
}

template<size_t count> requires (count <= 32)
void AtomicCounterBuffer<count>::onCreate()
{
	resize(count);
}

template<size_t count> requires (count <= 32)
template<size_t index> requires (index < count)
void AtomicCounterBuffer<count>::set(uint32_t value)
{
	ensureCreated();

	upload(&value, sizeof(uint32_t), index + sizeof(uint32_t));
}


// ============================================================================
// ============================================================================


template<typename T, size_t size> requires (size > 0 && sizeof(T) >= 4)
class MappedStorage : protected ShaderStorage<T> // T must be @interop struct
{
public:
	// TODO: add control over GL_MAP_COHERENT_BIT / GL_MAP_FLUSH_EXPLICIT_BIT use?
	inline MappedStorage(std::string_view name, BufferUsage default_usage=DynamicDraw) :
		ShaderStorage<T>(name, default_usage)
	{};

	using ShaderStorage<T>::setBindIndex;
	using ShaderStorage<T>::clear;
	using ShaderStorage<T>::elem_size;

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

template<typename T, size_t N> requires (N > 0 && sizeof(T) >= 4)
void MappedStorage<T, N>::flush()
{
	if(not this->id())  // calling this while still unmapped makes little sense
		return;
	glFlushMappedNamedBufferRange(this->id(), 0, N*sizeof(T));
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

template<typename T, size_t N> requires (N > 0 && sizeof(T) >= 4)
void MappedStorage<T, N>::onCreate()
{
	static constexpr auto flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT;
	static constexpr auto map_flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_FLUSH_EXPLICIT_BIT;

	glNamedBufferStorage(this->id(), N*sizeof(T), nullptr, flags);
	_data = static_cast<T *>(glMapNamedBufferRange(this->id(), 0, N*elem_size, map_flags));
}

} // RGL::buffer
