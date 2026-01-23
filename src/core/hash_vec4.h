#pragma once

#include <glm/vec4.hpp>
#include "hash_combine.h"

namespace std
{
template<>
struct hash<glm::vec4>
{
	[[nodiscard]] inline size_t operator()(const glm::vec4 &v) const
	{
		size_t h { 0 };
		h = hash_combine(h, v.x);
		h = hash_combine(h, v.y);
		h = hash_combine(h, v.z);
		h = hash_combine(h, v.w);
		return h;
	}
};

} // std


