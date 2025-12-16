#pragma once

#include <glm/vec4.hpp>
#include "hash_combine.h"

namespace std
{
template<>
struct hash<glm::vec4>
{
	size_t operator()(const glm::vec4 &v) const
	{
		size_t h { 0 };
		hash_combine(h, v.x);
		hash_combine(h, v.y);
		hash_combine(h, v.z);
		hash_combine(h, v.w);
		return h;
	}
};

} // std


