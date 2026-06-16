#pragma once

#include <glm/vec3.hpp>

#include "light_type.h"

namespace RGL
{

enum class LightType : uint_fast8_t;

static constexpr float s_default_shadow_compression = 0.3f;

namespace component
{

struct LightGeneral
{
	static LightGeneral create_point(const glm::vec3 color, float intensity, bool shadows, bool contact, bool volumetric=true) {
		return {
			.light_type = LightType::Point,
			.color = color,
			.intensity = intensity,
			.enabled = true,
			.shadow_caster = shadows,
			.contact_shadows = contact,
			.is_volumetric = volumetric,
			.has_surface = false,
			.fog = 1.f,
			.shadow_compression = s_default_shadow_compression,
			.shadow_index = LIGHT_NO_SHADOW,
		};
	}
	static LightGeneral create_directional(const glm::vec3 color, float intensity, bool shadows, bool contact, bool volumetric=true) {
		return {
			.light_type = LightType::Directional,
			.color = color,
			.intensity = intensity,
			.enabled = true,
			.shadow_caster = shadows,
			.contact_shadows = contact,
			.is_volumetric = volumetric,
			.has_surface = false,
			.fog = 1.f,
			.shadow_compression = s_default_shadow_compression,
			.shadow_index = LIGHT_NO_SHADOW,
		};
	}
	static LightGeneral create_spot(const glm::vec3 color, float intensity, bool shadows, bool contact, bool volumetric=true) {
		return {
			.light_type = LightType::Spot,
			.color = color,
			.intensity = intensity,
			.enabled = true,
			.shadow_caster = shadows,
			.contact_shadows = contact,
			.is_volumetric = volumetric,
			.has_surface = false,
			.fog = 1.f,
			.shadow_compression = s_default_shadow_compression,
			.shadow_index = LIGHT_NO_SHADOW,
		};
	}
	static LightGeneral create_rect(const glm::vec3 color, float intensity, bool volumetric=true) {
		return {
			.light_type = LightType::Rect,
			.color = color,
			.intensity = intensity,
			.enabled = true,
			.shadow_caster = false,
			.contact_shadows = false,
			.is_volumetric = volumetric,
			.has_surface = true,
			.fog = 1.f,
			.shadow_compression = 0,
			.shadow_index = LIGHT_NO_SHADOW,
		};
	}
	static LightGeneral create_tube(const glm::vec3 color, float intensity, bool volumetric=true) {
		return {
			.light_type = LightType::Tube,
			.color = color,
			.intensity = intensity,
			.enabled = true,
			.shadow_caster = false,
			.contact_shadows = false,
			.is_volumetric = volumetric,
			.has_surface = true,
			.fog = 1.f,
			.shadow_compression = 0,
			.shadow_index = LIGHT_NO_SHADOW,
		};
	}
	static LightGeneral create_sphere(const glm::vec3 color, float intensity, bool volumetric=true) {
		return {
			.light_type = LightType::Sphere,
			.color = color,
			.intensity = intensity,
			.enabled = true,
			.shadow_caster = false,
			.contact_shadows = false,
			.is_volumetric = volumetric,
			.has_surface = true,
			.fog = 1.f,
			.shadow_compression = 0,
			.shadow_index = LIGHT_NO_SHADOW,
		};
	}
	static LightGeneral create_disc(const glm::vec3 color, float intensity, bool volumetric=true) {
		return {
			.light_type = LightType::Disc,
			.color = color,
			.intensity = intensity,
			.enabled = true,
			.shadow_caster = false,
			.contact_shadows = false,
			.is_volumetric = volumetric,
			.has_surface = true,
			.fog = 1.f,
			.shadow_compression = 0,
			.shadow_index = LIGHT_NO_SHADOW,
		};
	}

	LightType light_type;       // LIGHT_TYPE_*
	glm::vec3 color;            // { 0, 0, 0 } to { 1, 1, 1 }
	float     intensity;        // >= 0
	bool      enabled;
	bool      shadow_caster;
	bool      contact_shadows;
	bool      is_volumetric;
	bool      has_surface;
	float     fog;              // >= 0
	float     shadow_compression; // [0, 1) (0 = full range)
	uint16_t  shadow_index;     // >= 0   OR  LIGHT_NO_SHADOW
};
static_assert(sizeof(LightGeneral) == 40);


} // component

} // RGL


#include "hash_combine.h"
#include "hash_vec3.h"   // IWYU pragma: keep

namespace std
{
template<>
struct hash<RGL::component::LightGeneral>
{
	[[nodiscard]] inline size_t operator()(const RGL::component::LightGeneral &general) const
	{
		size_t h { 0 };
		h = hash_combine(h, general.color);
		h = hash_combine(h, general.intensity);
		h = hash_combine(h, general.fog);
		return h;
	}
};

} // std
