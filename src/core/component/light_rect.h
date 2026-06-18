#pragma once

#include <glm/vec2.hpp>

namespace RGL::component
{

struct RectLight
{
	glm::vec2 size    { 1, 1 };
	bool double_sided { false };
};

} // RGL::component



#include "hash_combine.h"
#include "hash_vec2.h"   // IWYU pragma: keep

namespace std
{
template<>
struct hash<RGL::component::RectLight>
{
	[[nodiscard]] inline size_t operator()(const RGL::component::RectLight&rect) const
	{
		size_t h { 0 };
		h = hash_combine(h, rect.size);
		h = hash_combine(h, rect.double_sided);
		return h;
	}
};

} // std
