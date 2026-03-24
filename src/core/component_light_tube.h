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
