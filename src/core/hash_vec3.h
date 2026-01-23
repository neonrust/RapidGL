#pragma once

#include <glm/vec3.hpp>
#include "hash_combine.h"

namespace std
{
template<>
struct hash<glm::vec3>
{
	[[nodiscard]] inline size_t operator()(const glm::vec3 &v) const
	{
		size_t h { 0 };
		h = hash_combine(h, v.x);
		h = hash_combine(h, v.y);
		h = hash_combine(h, v.z);
		return h;
	}
};

} // std


