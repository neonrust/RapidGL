#pragma once

#include <glm/vec2.hpp>
#include "hash_combine.h"

namespace std
{
template<>
struct hash<glm::uvec2>
{
	[[nodiscard]] inline size_t operator()(const glm::uvec2 &v) const
	{
		size_t h { 0 };
		h = hash_combine(h, v.x);
		h = hash_combine(h, v.y);
		return h;
	}
};

} // std


