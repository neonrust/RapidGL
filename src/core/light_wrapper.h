#pragma once

#include "component/transform.h"
#include "component/light_general.h"
#include "component/light_point.h"
#include "component/light_directional.h"
#include "component/light_spot.h"
#include "component/light_rect.h"
#include "component/light_tube.h"
#include "component/light_sphere.h"
#include "component/light_disc.h"
#include "generated/shared-structs.h"
#include <variant>

namespace RGL
{

struct LightWrapper
{
	component::LightGeneral general;  // general.light_type contains which of the variants of 'light' is valid
	std::variant<
		component::PointLight,
		component::DirectionalLight,
		component::SpotLight,
		component::RectLight,
		component::TubeLight,
		component::SphereLight,
		component::DiscLight> light;
	component::Transform transform;
	GPULight gpu_light;
};

} // RGL