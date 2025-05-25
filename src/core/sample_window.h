#pragma once

#include "container_types.h"
#include <cstddef>
#include <numeric>


template<typename T, size_t size>
class SampleWindow
{
public:
	void add(T value);
	void clear();

	[[nodiscard]] T average(T def=T{0}) const noexcept;
	[[nodiscard]] T sum(T def=T{0}) const noexcept;
	[[nodiscard]] inline T min(T def=T{0}) const noexcept;
	[[nodiscard]] inline T max(T def=T{0}) const noexcept;
	[[nodiscard]] inline T first(T def=T{0}) const noexcept;
	[[nodiscard]] inline T last(T def=T{0}) const noexcept;

	[[nodiscard]] inline bool empty() const noexcept { return _samples.empty(); }
	[[nodiscard]] inline size_t num_samples() const noexcept { return _samples.size(); }

private:
	small_vec<T, size> _samples;
};

template<typename T, size_t size>
inline void SampleWindow<T, size>::add(T value)
{
	if(_samples.size() == size)
		_samples.erase(_samples.begin());
	else if(_samples.capacity() == 0)
		_samples.reserve(size);

	_samples.push_back(value);
}

template<typename T, size_t size>
inline void SampleWindow<T, size>::clear()
{
	_samples.clear();
}

template<typename T, size_t size>
inline T SampleWindow<T, size>::average(T def) const noexcept
{
	if(empty())
		return def;
	return sum() / num_samples();
}

template<typename T, size_t size>
inline T SampleWindow<T, size>::sum(T def) const noexcept
{
	if(empty())
		return def;
	return std::accumulate(_samples.begin(), _samples.end(), T{ 0 });
}

template<typename T, size_t size>
inline T SampleWindow<T, size>::min(T def) const noexcept
{
	if(empty())
		return def;
	return std::min_element(_samples.begin(), _samples.end());
}

template<typename T, size_t size>
inline T SampleWindow<T, size>::max(T def) const noexcept
{
	if(empty())
		return def;
	return std::max_element(_samples.begin(), _samples.end());
}

template<typename T, size_t size>
inline T SampleWindow<T, size>::first(T def) const noexcept
{
	if(empty())
		return def;
	return _samples.front();
}

template<typename T, size_t size>
inline T SampleWindow<T, size>::last(T def) const noexcept
{
	if(empty())
		return def;
	return _samples.back();;
}
