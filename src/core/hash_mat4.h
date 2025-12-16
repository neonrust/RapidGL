#pragma once

#include <glm/mat4x4.hpp>
#include "hash_combine.h"

namespace std
{
template<>
struct hash<glm::mat4>
{
	size_t operator()(const glm::mat4 &m) const
	{
		size_t h { 0 };
		hash_combine(h, m[0]);
		hash_combine(h, m[1]);
		hash_combine(h, m[2]);
		hash_combine(h, m[3]);
		return h;
	}
};

} // std



