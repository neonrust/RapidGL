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
