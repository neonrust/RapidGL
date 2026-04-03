#pragma once

#include <glm/vec3.hpp>

namespace RGL::component
{

struct TubeLight
{
	glm::vec3 half_extent { 0.2f };  // TODO: float is enough, implied a vector of specified length in some direction, e.g. X
	float thickness;
};

} // RGL::component

#include "hash_combine.h"
#include "hash_vec3.h"   // IWYU pragma: keep

namespace std
{
template<>
struct hash<RGL::component::TubeLight>
{
	[[nodiscard]] inline size_t operator()(const RGL::component::TubeLight &tube) const
	{
		size_t h { 0 };
		h = hash_combine(h, tube.half_extent);
		h = hash_combine(h, tube.thickness);
		return h;
	}
};

} // std
