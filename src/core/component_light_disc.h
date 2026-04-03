#pragma once

namespace RGL::component
{

struct DiscLight
{
	float radius  { 0.2f };  // > 0
};

} // RGL::component


#include "hash_combine.h"

namespace std
{
template<>
struct hash<RGL::component::DiscLight>
{
	[[nodiscard]] inline size_t operator()(const RGL::component::DiscLight &disc) const
	{
		size_t h { 0 };
		h = hash_combine(h, disc.radius);
		return h;
	}
};

} // std
