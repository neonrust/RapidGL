#pragma once

#include <functional>

template<typename T>
inline void hash_combine(std::size_t& seed, const T& v)
{
	seed ^= std::hash<T>{}(v) + size_t(0x9e3779b97f4a7c15) + (seed << 6) + (seed >> 2);
}
