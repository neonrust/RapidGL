#pragma once

namespace RGL
{

namespace component
{

struct SpotLight
{
	// half-angles, in radians
	float outer_angle  { 0.5f };  // >= 0
	float inner_angle  { 0.4f };  // 0 to outer_angle
};

} // component

} // RGL

#include "hash_combine.h"

namespace std
{
template<>
struct hash<RGL::component::SpotLight>
{
	[[nodiscard]] inline size_t operator()(const RGL::component::SpotLight &spot) const
	{
		size_t h { 0 };
		h = hash_combine(h, spot.outer_angle);
		h = hash_combine(h, spot.inner_angle);
		return h;
	}
};

} // std
