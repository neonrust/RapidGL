#pragma once

#include "animated_model.h"

#include <glm/mat4x4.hpp>
#include <memory>

struct DynamicObject
{
	inline DynamicObject() :
		DynamicObject(nullptr, glm::mat4(1))
	{}
	inline DynamicObject(const std::shared_ptr<RGL::AnimatedModel> &model_, const glm::mat4 & transform_) :
		transform(transform_),
		model    (model_) {}

	glm::mat4 transform;
	std::shared_ptr<RGL::AnimatedModel> model;
};
