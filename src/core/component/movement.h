#pragma once

#include <glm/vec3.hpp>

#include <optional>

namespace RGL::component
{

struct Movement
{
	glm::vec3 velocity { 0 };
	std::optional<glm::vec3> end_position;
};

} // RGL::component