#pragma once

#include <algorithm>
#include "ringbuffer.h"
#include <cstddef>
#include <numeric>


template<typename T, size_t size> requires (size > 0)
class SampleWindow
{
public:
	void add(T value);
	inline SampleWindow &operator += (T value) { add(value); return *this; }
	void clear();

	[[nodiscard]] T average(T init=T{0}) const noexcept;
	[[nodiscard]] T sum(T init=T{0}) const noexcept;
	[[nodiscard]] inline T min(T def=T{0}) const noexcept;
	[[nodiscard]] inline T max(T def=T{0}) const noexcept;
	[[nodiscard]] inline T first(T def=T{0}) const noexcept;
	// [[nodiscard]] inline std::basic_string_view<T> last(size_t count=T{0}) const noexcept;

	[[nodiscard]] inline bool empty() const noexcept { return _samples.empty(); }
	[[nodiscard]] inline size_t num_samples() const noexcept { return _samples.size(); }

private:
	RingBuffer<T, size> _samples;
};

template<typename T, size_t size> requires (size > 0)
inline void SampleWindow<T, size>::add(T value)
{
	_samples.push(value);
}

template<typename T, size_t size> requires (size > 0)
inline void SampleWindow<T, size>::clear()
{
	_samples.clear();
}

template<typename T, size_t size> requires (size > 0)
inline T SampleWindow<T, size>::average(T init) const noexcept
{
	if(empty())
		return init;
	return sum(init) / num_samples();
}

template<typename T, size_t size> requires (size > 0)
inline T SampleWindow<T, size>::sum(T init) const noexcept
{
	return std::accumulate(_samples.begin(), _samples.end(), init);
}

template<typename T, size_t size> requires (size > 0)
inline T SampleWindow<T, size>::min(T def) const noexcept
{
	if(empty())
		return def;
	return std::min_element(_samples.begin(), _samples.end());
}

template<typename T, size_t size> requires (size > 0)
inline T SampleWindow<T, size>::max(T def) const noexcept
{
	if(empty())
		return def;
	return std::max_element(_samples.begin(), _samples.end());
}

template<typename T, size_t size> requires (size > 0)
inline T SampleWindow<T, size>::first(T def) const noexcept
{
	if(empty())
		return def;
	return _samples.front();
}

// template<typename T, size_t size> requires (size > 0)
// inline std::basic_string_view<T> SampleWindow<T, size>::last(size_t count) const noexcept
// {
// 	if(empty())
// 		return {};
// 	if(count == 0)
// 		count = _samples.size();
// 	else
// 		count = std::min(count, _samples.size());

// 	return std::basic_string_view<T>(&*(_samples.end() - count), count);
// }
