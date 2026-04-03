#pragma once

namespace RGL::component
{

struct SphereLight
{
	float radius  { 0.2f };  // > 0
};

} // RGL::component

#include "hash_combine.h"

namespace std
{
template<>
struct hash<RGL::component::SphereLight>
{
	[[nodiscard]] inline size_t operator()(const RGL::component::SphereLight &sphere) const
	{
		size_t h { 0 };
		h = hash_combine(h, sphere.radius);
		return h;
	}
};

} // std

