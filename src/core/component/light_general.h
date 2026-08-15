#pragma once

#include <glm/vec3.hpp>

#include <string>
#include <cstring>

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
			.shadow_compression = 0,//s_default_shadow_compression,
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

	void set_name(std::string_view name_);

	static constexpr size_t NAME_LEN = 15;
	char      name[NAME_LEN + 1] { '\0' };
	LightType light_type         { LightType::Point };
	glm::vec3 color              { 1, 1, 1 };
	float     intensity          { 10.f };     // >= 0
	bool      enabled            { true };
	bool      shadow_caster      { true };
	bool      contact_shadows    { false };
	bool      is_volumetric      { false };
	bool      has_surface        { false };
	float     fog                { 1.f };            // >= 0
	float     shadow_compression { 0.f }; // [0, 1) (0 = full range)
	uint16_t  shadow_index { LIGHT_NO_SHADOW };  // >= 0   OR  LIGHT_NO_SHADOW
};
static_assert(sizeof(LightGeneral) == 56);

inline void LightGeneral::set_name(std::string_view name_)
{
	if(name_.size() > 0)
	{
		name_ = name_.substr(0, std::min(name_.size(), NAME_LEN));
		std::strcpy(name, name_.data());
	}
	name[name_.size()] = '\0';
}


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
		// the other parametes?
		return h;
	}
};

} // std
