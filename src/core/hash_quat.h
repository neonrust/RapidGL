#pragma once

#include <glm/gtc/quaternion.hpp>
#include "hash_combine.h"

namespace std
{
template<>
struct hash<glm::quat>
{
	[[nodiscard]] inline size_t operator()(const glm::quat &q) const
	{
		size_t h { 0 };
		h = hash_combine(h, q.x);
		h = hash_combine(h, q.y);
		h = hash_combine(h, q.z);
		h = hash_combine(h, q.w);
		return h;
	}
};

} // std


