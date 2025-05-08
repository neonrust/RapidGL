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

	T average() const;
	T sum() const;
	T min() const;
	T max() const;

	inline size_t num_samples() const { return _samples.size(); }

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
inline T SampleWindow<T, size>::average() const
{
	return sum() / num_samples();
}

template<typename T, size_t size>
inline T SampleWindow<T, size>::sum() const
{
	return std::accumulate(_samples.begin(), _samples.end(), T{ 0 });
}

template<typename T, size_t size>
inline T SampleWindow<T, size>::min() const
{
	return std::min_element(_samples.begin(), _samples.end());
}

template<typename T, size_t size>
inline T SampleWindow<T, size>::max() const
{
	return std::max_element(_samples.begin(), _samples.end());
}
