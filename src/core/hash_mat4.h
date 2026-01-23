#pragma once

#include <glm/mat4x4.hpp>
#include "hash_combine.h"
#include "hash_vec4.h"   // IWYU pragma: keep

namespace std
{
template<>
struct hash<glm::mat4>
{
	[[nodiscard]] inline size_t operator()(const glm::mat4 &m) const
	{
		size_t h { 0 };
		h = hash_combine(h, m[0]);
		h = hash_combine(h, m[1]);
		h = hash_combine(h, m[2]);
		h = hash_combine(h, m[3]);
		return h;
	}
};

} // std



