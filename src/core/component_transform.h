#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL   // for glm::decompose
#include <glm/gtx/matrix_decompose.hpp>


namespace component
{

struct Transform
{
	glm::vec3 position;
	glm::quat orientation;
	glm::vec3 scale;

	inline Transform(const glm::mat4 &tfm)
	{
		glm::vec3 skew;        // not used
		glm::vec4 perspective; // not used

		glm::decompose(tfm, scale, orientation, position, skew, perspective);
	}

	inline operator glm::mat4 () const
	{
		auto m = glm::mat4(1);
		m = glm::translate(glm::mat4(1), position);
		m = m * glm::mat4_cast(orientation);
		m = glm::scale(m, scale);
		return m;
	}

	inline float max_scale() const {
		return glm::max(scale.x, glm::max(scale.y, scale.z));
	}
};

} // component
