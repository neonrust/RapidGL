#pragma once

#include "static_model.h"

#include <glm/mat4x4.hpp>
#include <memory>

struct StaticObject
{
	inline StaticObject() :
		StaticObject(nullptr, glm::mat4(1))
	{}
	inline StaticObject(const std::shared_ptr<RGL::StaticModel> & model_, const glm::mat4 & transform_) :
		transform(transform_),
		model    (model_) {}

	glm::mat4 transform;
	std::shared_ptr<RGL::StaticModel> model;
};
