#pragma once

#include <glm/vec3.hpp>
#include "hash_combine.h"

namespace std
{
template<>
struct hash<glm::vec3>
{
	size_t operator()(const glm::vec3 &v) const
	{
		size_t h { 0 };
		hash_combine(h, v.x);
		hash_combine(h, v.y);
		hash_combine(h, v.z);
		return h;
	}
};

} // std


