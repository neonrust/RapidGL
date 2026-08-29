#pragma once

#include "game_time.h"

#include "animation/animation_curve.h"

#include <glm/vec3.hpp>

namespace RGL::anim
{

enum EndState { Clamp, Reset };

} // EGL::anim

namespace RGL::component
{

struct Animation
{
	struct
	{
		std::array<anim::curve_ref<>, 3> position;
		std::array<anim::curve_ref<>, 3> orientation;
		std::array<anim::curve_ref<>, 3> scale;
		uint_fast16_t mask { 0 };
	} curves;

	seconds_f end_time { 0 };
	uint32_t total_loops { 1 }; // 'InfiniteLoops' or number of loops

	anim::EndState end_state { anim::EndState::Clamp };

	const std::string name;
};

} // RGL::component
