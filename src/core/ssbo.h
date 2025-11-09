#pragma once

#include "glad/glad.h"

#include <memory>
#include <vector>
#include <cassert>
#include <span>
#include <print>
#include <cstring>  // std::memset
#include <iterator>

#include "buffer.h"

namespace RGL::buffer
{

template<typename Iter, typename T>
concept IteratorOf = std::contiguous_iterator<Iter> &&
	std::same_as<std::remove_cvref_t<decltype(*std::declval<Iter>())>, T>;

template<typename R, typename T>
concept ContiguousRangeOf =
		std::ranges::contiguous_range<R> &&
		std::same_as<std::ranges::range_value_t<R>, T>;

template<typename T>
class Storage : public Buffer
{
public:
	using value_type = T;

private:
	static constexpr size_t type_size();
public:
	static constexpr size_t elem_size = type_size();

public:
	Storage(std::string_view name, BufferUsage default_usage=DynamicDraw) :
		Buffer(name, GL_SHADER_STORAGE_BUFFER, default_usage)
	{
	}

	void bindIndirectDispatch() const;//requires (std::is_same_v<T, glm::uvec3>);

	template<ContiguousRangeOf<T> R>
	void set(const R &data);
	bool set(size_t index, const T &item);
	template<IteratorOf<T> Iter> // TODO: must be contiguous!
	void set(Iter begin, Iter end, size_t start_index=0);

	std::vector<T> download(size_t offset=0, size_t count=0);
	bool download(std::vector<T> &destination, size_t offset=0, size_t count=0);

	inline size_t size() const { return _size; }
	void resize(size_t size);

	void copyTo(Storage<T> &dest, size_t count=0, size_t readStart=0, size_t writeStart=0) const;


	class View : public std::span<const T, std::dynamic_extent>
	{
		friend class Storage;
	public:
		virtual ~View();
	private:
		View(Storage<T> *buffer, const T *start);
	private:
		Storage<T> *_buffer;
	};

	std::unique_ptr<const typename Storage<T>::View> view();

protected:
	void releaseView();

private:
	size_t _size { 0 };
	bool _view_active { false };
};


template<typename T>
void Storage<T>::bindIndirectDispatch() const //requires (std::is_same_v<T, glm::uvec3>)
{
	glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, id());
}

template<typename T>
template<ContiguousRangeOf<T> R>
void Storage<T>::set(const R &data)
{
	ensureCreated();

	// TODO: double-buffer (similar to LightManager) ?

	upload(data.data(), data.size() * elem_size);
	_size = data.size();
}

template<typename T>
bool Storage<T>::set(size_t index, const T &item)
{
	ensureCreated();

	assert(_size > 0);
	assert(index < _size);
	if(index < _size)
		upload(&item, elem_size, index * elem_size);

	return index < _size;
}

template<typename T>
std::vector<T> Storage<T>::download(size_t offset, size_t count)
{
	std::vector<T> items;
	download(items, offset, count);
	return items;
}

template<typename T>
inline bool Storage<T>::download(std::vector<T> &destination, size_t offset, size_t count)
{
	if(count == 0)
		count = _size - offset;
	else if(offset + count > _size)
		count = _size - offset;
	destination.resize(count);
	glGetNamedBufferSubData(id(), sizeof(T)*offset, sizeof(T)*count, destination.data());
	return true;
}

template<typename T>
template<IteratorOf<T> Iter>
void Storage<T>::set(Iter begin, Iter end, size_t start_index)
{
	ensureCreated();

	const auto count = std::distance(begin, end);

	// TODO: if range flows outside current buffer?
	//   truncate iterator range
	//   or resize the buffer
	assert(_size >= start_index + count);

	upload(&*begin, count * elem_size, start_index * elem_size);
}

template<typename T>
void Storage<T>::resize(size_t size)
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
void Storage<T>::copyTo(Storage<T> &dest, size_t count, size_t readStart, size_t writeStart) const
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
std::unique_ptr<const typename Storage<T>::View> Storage<T>::view()
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
constexpr size_t Storage<T>::type_size()
{
	if constexpr (_private::HasSize<T>)
		return T::_struct_size;
	else
		return sizeof(T);
}

template<typename T>
void Storage<T>::releaseView()
{
	assert(_view_active);
	glUnmapNamedBuffer(id());
	_view_active = false;
}

template<typename T>
inline Storage<T>::View::View(Storage<T> *buffer, const T *start) :
	std::span<const T, std::dynamic_extent>(start, buffer->size()),
	_buffer(buffer)
{
}

template<typename T>
Storage<T>::View::~View()
{
	_buffer->releaseView();
}

// ============================================================================
// ============================================================================

template<size_t count=1> requires (count <= 32)
class AtomicCounters : protected Storage<uint32_t>
{
public:
	AtomicCounters(std::string_view name, BufferUsage usage=DefaultUsage);

	void clear();

	template<size_t index> requires (index < count)
	void set(uint32_t value);

protected:
	void onCreate() override;
};

template<size_t count> requires (count <= 32)
inline AtomicCounters<count>::AtomicCounters(std::string_view name, BufferUsage usage) :
	Storage<uint32_t>(name, usage)
{
	// TODO: set_buffer_type ?
	_buffer_type = GL_ATOMIC_COUNTER_BUFFER;
}

template<size_t count> requires (count <= 32)
void AtomicCounters<count>::clear()
{
	ensureCreated();

	GLuint values[count];
	std::memset(&values, sizeof(uint32_t)*count, 0);

	upload(values, sizeof(values));
}

template<size_t count> requires (count <= 32)
void AtomicCounters<count>::onCreate()
{
	resize(count);
}

template<size_t count> requires (count <= 32)
template<size_t index> requires (index < count)
void AtomicCounters<count>::set(uint32_t value)
{
	ensureCreated();

	upload(&value, sizeof(uint32_t), index + sizeof(uint32_t));
}


// ============================================================================
// ============================================================================


template<typename T, size_t size> requires (size > 0 && sizeof(T) >= 4)
class Mapped : protected Storage<T> // T must be @interop struct
{
public:
	// TODO: add control over GL_MAP_COHERENT_BIT / GL_MAP_FLUSH_EXPLICIT_BIT use?
	inline Mapped(std::string_view name, BufferUsage default_usage=DynamicDraw) :
		Storage<T>(name, default_usage)
	{};

	using Storage<T>::bindAt;
	using Storage<T>::clear;
	using Storage<T>::elem_size;

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
void Mapped<T, N>::flush()
{
	if(not this->id())  // calling this while still unmapped makes little sense
		return;
	glFlushMappedNamedBufferRange(this->id(), 0, N*sizeof(T));
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

template<typename T, size_t N> requires (N > 0 && sizeof(T) >= 4)
void Mapped<T, N>::onCreate()
{
	static constexpr auto flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT;
	static constexpr auto map_flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_FLUSH_EXPLICIT_BIT;

	glNamedBufferStorage(this->id(), N*sizeof(T), nullptr, flags);
	_data = static_cast<T *>(glMapNamedBufferRange(this->id(), 0, N*elem_size, map_flags));
}

// ============================================================================
// ============================================================================

template <typename S>
concept StorageLike = requires { typename S::value_type; }
	and std::is_base_of_v<Storage<typename S::value_type>, S>;

template<StorageLike S, size_t N> requires (N > 1)
class Cycle
{
public:
	using value_type = typename S::value_type;

public:
	Cycle();

	inline void bindActiveAt(GLuint id) { _buffer[_active].bindAt(id); }

	template<ContiguousRangeOf<value_type> R>
	inline void set(const R &data) { _buffer[_active].set(data); }
	inline bool set(size_t index, const value_type &item) { _buffer[_active].set(index, item); }
	template<IteratorOf<value_type> Iter>
	inline void set(Iter begin, Iter end, size_t start_index=0) { _buffer[_active].set(begin, end, start_index); }

	inline size_t size() const { return _buffer[0]._size; }
	inline void resize(size_t size) {
		for(auto idx = 0u; idx < N; ++idx)
			_buffer[N].resize(size);
	}

	inline uint32_t active() const { return _active; }

	inline uint32_t cycle()
	{
		const auto ready = _active;
		_active = (_active + 1) % N;
		return ready;
	}

private:
	S _buffer[N];
	uint32_t _active { 0 };
};

} // RGL::buffer
