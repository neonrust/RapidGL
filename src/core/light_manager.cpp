#include "light_manager.h"

#include "buffer_binds.h"
#include "light_constants.h"

#include "component_transform.h"
#include "component_light_general.h"
#include "component_light_point.h"
#include "component_light_directional.h"
#include "component_light_spot.h"
#include "component_light_rect.h"
#include "component_light_tube.h"
#include "component_light_sphere.h"
#include "component_light_disc.h"

#include "light_wrapper.h"
#include "hash_combine.h"
// #include "scoped_timer.h"
#include "hash_vec3.h"  // IWYU pragma: keep
#include "hash_vec4.h"  // IWYU pragma: keep
#include "log.h"

// #include <chrono>
#include <ranges>


using namespace std::literals;
using namespace std::chrono;

namespace RGL
{

// textual names for each light type, see light_constants.h
static const std::string_view s_light_type_names[] {
	"point"sv,
	"directional"sv,
	"spot"sv,
	"rect"sv,
	"tube"sv,
	"sphere"sv,
	"disc"sv,
};
static_assert(LIGHT_TYPE__COUNT == 7);

float LightManager::s_radius_power { 0.7f };

LightManager::LightManager(entt::registry &entities) :
	_lights_ssbo("lights"sv),
	_entities(entities)
{
	_lights_ssbo.bindAt(SSBO_BIND_LIGHTS);

	reserve(1024);

	// byt default, no limits on number of lights
	for(auto idx = 0u; idx < LIGHT_TYPE__COUNT; ++idx)
	{
		_light_type_limit[idx] = 0;
		_num_light_type[idx] = 0;
	}

	_connect_signals();
}

void LightManager::reserve(size_t count)
{
	_id_to_index.reserve(count);
	_index_to_id.reserve(count);
	_dirty.reserve(count);
	_dirty_list.reserve(count);
	_lights.reserve(count);
}

bool LightManager::remove(LightID light_id)
{
	if(not _id_to_index.contains(light_id))
		return false;

	const auto general = _entities.get<component::LightGeneral>(entt::entity(light_id));
	--_num_light_type[uint32_t(general.light_type)];

	_entities.destroy(entt::entity(light_id)); // triggers _light_removed()

	if(light_id == _sun_light_id)
	{
		_sun_light_id = NO_LIGHT_ID;
		_sun_light_intensity = 0.f;

		if(_num_light_type[uint32_t(LightType::Directional)] > 0)
		{
			// use strongest directional light as sun, if any
			auto dir_lights = _entities.view<component::LightGeneral, component::DirectionalLight>();
			LightID strongest_id { NO_LIGHT_ID };
			float strongest { 0.f };
			for(const auto &[light_id, general, _]: dir_lights.each())
			{
				if(general.intensity > strongest)
				{
					strongest = general.intensity;
					strongest_id = LightID(light_id);
				}
			}
			if(strongest_id != NO_LIGHT_ID)
			{
				_sun_light_id = strongest_id;
				_sun_light_intensity = strongest;
			}
		}
	}

	return true;
}

void LightManager::clear()
{
	_id_to_index.clear();
	_index_to_id.clear();
	_lights.clear();
	_dirty.clear();
	_dirty_list.clear();

	_sun_light_id = NO_LIGHT_ID;
	_sun_light_intensity = 0.f;

	for(auto idx = 0u; idx < LIGHT_TYPE__COUNT; ++idx)
		_num_light_type[idx] = 0;
}

// std::tuple<LightID, const GPULight &> LightManager::gpu_at(LightIndex light_index) const
// {
// 	if(light_index >= _index_to_id.size())
// 	{
// 		assert(false);
// 		throw std::out_of_range("index not found");
// 	}
// 	const auto light_id = _index_to_id[light_index];
// 	const auto &L = _lights[light_index];

// 	return std::tie(light_id, L);
// }

// void LightManager::set(LightID uuid, const GPULight &L)
// {
// 	auto found = _id_to_index.find(uuid);
// 	assert(found != _id_to_index.end());

// 	const auto light_index = found->second;

// 	if(IS_SPOT_LIGHT(L))
// 	{
// 		auto Lspot = L;
// 		_update_spot_bounds(Lspot);
// 		_lights[light_index] = Lspot;
// 	}
// 	else
// 		_lights[light_index] = L;

// 	_set_dirty_id(light_index);
// }

void LightManager::flush()
{
	// more/less lights than before; upload all  (hpefully, this doesn't happen often)
	if(_lights.size() != _lights_ssbo.size() or _dirty.size() == _lights.size())
	{
		_gpu_build(0, LightIndex(_lights.size()));
		_lights_ssbo.set(_lights);
	}
	else if(not _dirty.empty())
	{
		// no lights were added or removed, but some are dirty

		// make as few .update() calls as possible to the SSBO, using contiguous ranges
		std::ranges::sort(_dirty_list);

		auto contiguous = [](auto a, auto b){
			return b == a + 1;
		};
		for(auto subrange : _dirty_list | std::views::chunk_by(contiguous))
		{
			auto s = *subrange.begin();
			auto e = *std::prev(subrange.end());
			if (s == e)
			{
				_gpu_build(s);
				_lights_ssbo.set(s, _lights[s]);
			}
			else
			{
				_gpu_build(s, e);
				_lights_ssbo.set(_lights.begin() + s, _lights.begin() + e+ 1, s);
			}
		}
	}
	_dirty.clear();
	_dirty_list.clear();
}

uint_fast16_t LightManager::shadow_index(LightID light_id) const
{
	return _entities.get<component::LightGeneral>(entt::entity(light_id)).shadow_index;
}

void LightManager::set_shadow_index(LightID light_id, uint16_t shadow_index)
{
	// TODO: maybe modify LightGeneral and pthe GPULight directly ?
	//   but need also to write to SSBO

	const auto light_ent = entt::entity(light_id);
	assert(_id_to_index.contains(light_id) and _entities.all_of<component::LightGeneral>(light_ent));

	auto &general = _entities.get<component::LightGeneral>(light_ent);
	if(general.shadow_index != shadow_index)
	{
		general.shadow_index = shadow_index;
		if(general.enabled)
			_general_changed(_entities, light_ent);
	}
}

void LightManager::clear_shadow_index(LightID light_id)
{
	// TODO: maybe modify LightGeneral and pthe GPULight directly ?
	//   but need also to write to SSBO

	const auto light_ent = entt::entity(light_id);
	assert(_id_to_index.contains(light_id) and _entities.all_of<component::LightGeneral>(light_ent));

	auto &general = _entities.get<component::LightGeneral>(light_ent);
	if(general.shadow_index != LIGHT_NO_SHADOW)
	{
		general.shadow_index = LIGHT_NO_SHADOW;
		if(general.enabled)
			_general_changed(_entities, light_ent);
	}
}

bool LightManager::is_enabled(LightID light_id) const
{
	const auto &general = _entities.get<component::LightGeneral>(entt::entity(light_id));
	return general.enabled;
}

void LightManager::set_enabled(LightID light_id, bool enabled)
{
	assert(_id_to_index.contains(light_id) and _entities.all_of<component::LightGeneral>(entt::entity(light_id)));
	const auto light_ent = entt::entity(light_id);

	auto &general = _entities.get<component::LightGeneral>(light_ent);
	if(general.enabled != enabled)
	{
		Log::debug("{{{}}} {}abled", light_id, enabled?"en":"dis");
		general.enabled = enabled;
		_general_changed(_entities, light_ent);
	}
}

void LightManager::set_color(LightID light_id, const glm::vec3 &color)
{
	assert(_id_to_index.contains(light_id) and _entities.all_of<component::LightGeneral>(entt::entity(light_id)));
	const auto light_ent = entt::entity(light_id);

	auto sat_color = glm::saturate(color);

	auto &general = _entities.get<component::LightGeneral>(light_ent);
	if(sat_color != general.color)
	{
		general.color = sat_color;
		if(general.enabled)
			_general_changed(_entities, light_ent);
	}
}

void LightManager::set_intensity(LightID light_id, float intensity)
{
	assert(_id_to_index.contains(light_id) and _entities.all_of<component::LightGeneral>(entt::entity(light_id)));
	const auto light_ent = entt::entity(light_id);

	auto &general = _entities.get<component::LightGeneral>(light_ent);
	if(general.intensity != intensity)
	{
		general.intensity = intensity;
		if(general.enabled)
			_general_changed(_entities, light_ent);
	}
}

void LightManager::modify_flags(LightID light_id, uint32_t set_flags, uint32_t clear_flags)
{
	assert(_id_to_index.contains(light_id) and _entities.all_of<component::LightGeneral>(entt::entity(light_id)));
	const auto light_ent = entt::entity(light_id);

	auto &general = _entities.get<component::LightGeneral>(light_ent);

	static constexpr uint32_t shadow_bits = LIGHT_SHADOW_CASTER;
	static constexpr uint32_t contact_bits = LIGHT_CONTACT_SHADOWS;
	static constexpr uint32_t volume_bits = LIGHT_VOLUMETRIC;

	const auto cast_shadow = (((general.shadow_caster? shadow_bits: 0) | (set_flags & shadow_bits)) & (~clear_flags & shadow_bits)) > 0;
	const auto contact_shadows = (((general.contact_shadows? contact_bits: 0) | (set_flags & contact_bits)) & (~clear_flags & contact_bits)) > 0;
	const auto volumetric = (((general.is_volumetric? volume_bits: 0) | (set_flags & volume_bits)) & (~clear_flags & volume_bits)) > 0;

	if(general.shadow_caster != cast_shadow
		or general.contact_shadows != contact_shadows
		or general.is_volumetric != volumetric)
	{
		general.shadow_caster = cast_shadow;
		general.contact_shadows = contact_shadows;
		general.is_volumetric = volumetric;
		if(general.enabled)
			_general_changed(_entities, light_ent);
	}
}

void LightManager::set_direction(LightID light_id, const glm::vec3 &direction)
{
	assert(_id_to_index.contains(light_id) and _entities.all_of<component::Transform>(entt::entity(light_id)));
	_entities.patch<component::Transform>(entt::entity(light_id), [&direction](auto &transform) {
		transform.set_orientation(glm::rotation(-AXIS_Z, direction));
	});
}

void LightManager::set_spot_angle(LightID light_id, float outer_angle, float inner_angle)
{
	assert(_id_to_index.contains(light_id) and _entities.all_of<component::LightGeneral>(entt::entity(light_id)));
	const auto light_ent = entt::entity(light_id);

	auto &spot = _entities.get<component::SpotLight>(light_ent);

	if(inner_angle < 0)
		inner_angle = spot.inner_angle;

	inner_angle = std::min(inner_angle, outer_angle);

	if(spot.outer_angle != outer_angle or spot.inner_angle != inner_angle)
	{
		spot.outer_angle = outer_angle;
		spot.inner_angle = inner_angle;
		_spot_changed(_entities, light_ent);
	}
}

float LightManager::_scale_spot_intensity(float base_intensity, const component::SpotLight &spot)
{
	return base_intensity * _spot_intensity_multiplier(spot.outer_angle);
}

float LightManager::_spot_intensity_multiplier(float angle)
{
	// const auto inv_cos_ref_angle = 1.f - std::cos(_private::s_spot_reference_angle);
	// return inv_cos_ref_angle / (1.f - std::cos(angle));

		   // a theoretical, ideal spot with 90 degrees angle (i.e. half a sphere)
		   //   would have double the intensity of a point light.
		   // same spot with angle zero would have infinite intensity.
	static const auto max_mult = 1.5f; // ideal spot would be 2
	return max_mult / std::max(1.f - std::cos(angle), 1e-4f);
	// also try: optimal_max / sin(angle)
}

float LightManager::_intensity_to_range(float intensity, float area)
{
	assert(intensity >= 0);
	assert(area >= 0);
	return std::pow(intensity, s_radius_power) * (1 + area);
}

// void LightManager::transform(GPULight &L, const glm::quat &rotate)
// {
// 	switch(GET_LIGHT_TYPE(L))
// 	{
// 	case LightType::Directional:
// 	case LightType::Spot:
// 	case LightType::Disc:
// 		set_direction(L, rotate * L.direction);
// 		break;
// 	case LightType::Rect:
// 	{
// 		L.shape_data[0] = rotate * L.shape_data[0];
// 		L.shape_data[1] = rotate * L.shape_data[1];
// 		L.shape_data[2] = rotate * L.shape_data[2];
// 		L.shape_data[3] = rotate * L.shape_data[3];
// 		// also update shape_data[4] (orientation)
// 		const auto &qvec = L.shape_data[4];
// 		const auto orientation = rotate * glm::quat(qvec.w, qvec.x, qvec.y, qvec.z);
// 		L.shape_data[4] = glm::vec4(orientation.x, orientation.y, orientation.z, orientation.w);
// 	}
// 	break;
// 	case LightType::Tube:
// 	{
// 		L.shape_data[0] = rotate * L.shape_data[0];
// 		L.shape_data[1] = rotate * L.shape_data[1];
// 		// also update shape_data[4] (orientation)
// 		const auto &qvec = L.shape_data[4];
// 		const auto orientation = rotate * glm::quat(qvec.w, qvec.x, qvec.y, qvec.z);
// 		L.shape_data[4] = glm::vec4(orientation.x, orientation.y, orientation.z, orientation.w);
// 	}
// 	break;
// 	default:
// 		// other light types makes little sense?
// 		assert(false);
// 	}
// }

// bounds::Sphere LightManager::light_bounds(const GPULight &L) const
// {
// 	assert(not IS_DIR_LIGHT(L));
// 	if(IS_DIR_LIGHT(L))
// 		return {};

// 	auto bounds_center = L.position;
// 	auto bounds_radius = L.affect_radius;

// 	if(IS_SPOT_LIGHT(L))
// 	{
// 		bounds_center += L.direction * L.spot_bounds_radius;
// 		bounds_radius = L.spot_bounds_radius;
// 	}

// 	return bounds::Sphere(bounds_center, bounds_radius);
// }

/*
void LightManager::add(const GPULight &L, LightID light_id)
{
	const auto next_index = LightIndex(_lights.size());

	const auto light_type = GET_LIGHT_TYPE(L);
	++_num_light_type[light_type];

	if(light_type == LightType::Spot)
	{
		auto Lspot = L;
		_update_spot_bounds(Lspot);
		_lights.push_back(Lspot);
		create_components(light_id, Lspot);
	}
	else
	{
		create_components(light_id, L);
		_lights.push_back(L);
	}

	_id_to_index[light_id] = next_index;
	_index_to_id[next_index] = light_id;

	_dirty.insert(next_index);
}
*/

void LightManager::_disconnect_signals()
{
	for(auto &conn: _signals)
		conn.release();
}

void LightManager::_connect_signals()
{
	_signals[0] = _entities.on_construct<component::LightGeneral>().connect<&LightManager::_light_added>(this);
	_signals[1] = _entities.on_destroy<  component::LightGeneral>().connect<&LightManager::_light_removed>(this);

	_signals[3] = _entities.on_update<   component::Transform>().connect<&LightManager::_light_moved>(this);
	_signals[2] = _entities.on_update<   component::LightGeneral>().connect<&LightManager::_general_changed>(this);
	_signals[3] = _entities.on_update<   component::SpotLight>  ().connect<&LightManager::_spot_changed>(this);
	_signals[3] = _entities.on_update<   component::RectLight>  ().connect<&LightManager::_rect_changed>(this);
	_signals[3] = _entities.on_update<   component::TubeLight>  ().connect<&LightManager::_tube_changed>(this);
	_signals[3] = _entities.on_update<   component::SphereLight>().connect<&LightManager::_sphere_changed>(this);
	_signals[3] = _entities.on_update<   component::DiscLight>  ().connect<&LightManager::_disc_changed>(this);
}

void LightManager::_light_added(entt::registry &, entt::entity light_ent)
{
	const auto light_id = LightID(light_ent);

	Log::debug("{{{}}} _light_added", light_id);

	assert(not _id_to_index.contains(light_id));
	if(_id_to_index.contains(light_id))
	{
		Log::error("_light_added: already exists: {}", light_id);
		return;
	}

	const auto light_index = LightIndex(_lights.size());

	_id_to_index[light_id] = light_index;
	_index_to_id.push_back(light_id);

	_lights.emplace_back();

	_gpu_build(light_index);
}

void LightManager::_light_removed(entt::registry &, entt::entity light_ent)
{
	const auto light_id = LightID(light_ent);

	Log::debug("{{{}}} _light_removed", light_id);

	auto found = _id_to_index.find(light_id);
	assert(found != _id_to_index.end());
	if(found == _id_to_index.end())
	{
		Log::error("_light_removed: no such light: {}", light_id);
		return;
	}

	// move last light in the list into the removed light's entry
	//   we do this by simply pointing <last indexed light>'s id to the index of the removed light
	//   thus minimizing SSBO changes (only one entry changes)

	const auto removed_index = found->second;
	const auto last_index_id = _index_to_id.back();

	_id_to_index.erase(found);

	 // the last-index light is now at this index
	_id_to_index[last_index_id] = removed_index;
	_set_dirty_index(removed_index);  // where the now-moved light reside

	// truncate CPU & GPU lists
	_lights.resize(_id_to_index.size());
	_lights_ssbo.resize(_id_to_index.size());
}

void LightManager::_general_changed(entt::registry &, entt::entity light_ent)
{
	const auto light_id = LightID(light_ent);

	// Log::debug("{{{}}} _general_changed", light_id);
	_set_dirty_id(light_id);

	// auto found = _id_to_index.find(light_id);
	// assert(found != _id_to_index.end());
	// if(found == _id_to_index.end())
	// {
	// 	Log::error("_general_changed: no such light: {}", light_id);
	// 	return;
	// }

	// const auto light_index = found->second;
	// auto &L = _lights[light_index];

	// const auto &general   = _entities.get<component::LightGeneral>(light_ent);

	// const auto old_intensity = L.intensity; // will be overwritten by _set_general()

	// // same as create_gpu_light()
	// _gpu_set_general(L, general);

	// // update intensity, and therefore also affect_radius
	// if(L.intensity != old_intensity)
	// {
	// 	// new intensity -> affect_radius -> bounds

	// 	// TODO: need area; depends on light type
	// 	L.affect_radius = _intensity_to_range(L.intensity, general.light_type);
	// }

}

void LightManager::_spot_changed(entt::registry &, entt::entity light_ent)
{
	const auto light_id = LightID(light_ent);

	Log::debug("{{{}}} _spot_changed", light_id);
	_set_dirty_id(light_id);

	// auto found = _id_to_index.find(light_id);
	// assert(found != _id_to_index.end());
	// if(found != _id_to_index.end())
	// {
	// 	Log::error("_spot_changed: no such light: {}", light_id);
	// 	return;
	// }
	// const auto light_index = found->second;
	// auto L = _lights[light_index];

	// const auto &spot = _entities.get<component::SpotLight>(light_ent);

	// if(spot.outer_angle != L.outer_angle)
	// {
	// 	// TODO: preferrably, don't retrieve LightGeneral twice...

	// 	const auto &general = _entities.get<component::LightGeneral>(light_ent);
	// 	const auto &[new_intensity, new_radius] = _on_spot_angle_changed(spot, general, L.outer_angle);
	// 	// new outer angle -> new intensity -> affect_radius -> bounds

	// 	// triggers _general_changed -> bounds recalc
	// 	_entities.patch<component::LightGeneral>(light_ent, [new_intensity, new_radius](auto &general) {
	// 		general.intensity = new_intensity;
	// 	});
	// }
}

void LightManager::_rect_changed(entt::registry &, entt::entity light_ent)
{
	const auto light_id = LightID(light_ent);

	Log::debug("{{{}}} _rect_changed", light_id);
	_set_dirty_id(light_id);

	// const auto &rect = _entities.get<component::RectLight>(light_ent);
}

void LightManager::_tube_changed(entt::registry &, entt::entity light_ent)
{
	const auto light_id = LightID(light_ent);

	Log::debug("{{{}}} _tube_changed", light_id);
	_set_dirty_id(light_id);

	// const auto &tube = _entities.get<component::TubeLight>(light_ent);
}

void LightManager::_sphere_changed(entt::registry &, entt::entity light_ent)
{
	const auto light_id = LightID(light_ent);

	Log::debug("{{{}}} _sphere_changed", light_id);
	_set_dirty_id(light_id);

	// const auto &sphere = _entities.get<component::SphereLight>(light_ent);
}

void LightManager::_disc_changed(entt::registry &, entt::entity light_ent)
{
	const auto light_id = LightID(light_ent);

	Log::debug("{{{}}} _disc_changed", light_id);
	_set_dirty_id(light_id);

	// const auto &disc = _entities.get<component::DiscLight>(light_ent);
}

void LightManager::_light_moved(entt::registry &, entt::entity light_ent)
{
	const auto light_id = LightID(light_ent);

	// Log::debug("{{{}}} _light_moved", light_id);
	_set_dirty_id(light_id);

	// auto found = _id_to_index.find(light_id);
	// assert(found != _id_to_index.end());
	// if(found == _id_to_index.end())
	// {
	// 	Log::error("changed light not in index: {}", light_id);
	// 	return;
	// }
	// auto light_index = found->second;
	// auto &L = _lights[light_index];

	// const auto &transform = _entities.get<component::Transform>(light_ent);
	// _gpu_set_spatial(L, light_id, transform);

	// _set_dirty_index(light_index);
}

// void LightManager::_update_spot_bounds(GPULight &L)
// {
// 	// calculate minimal sphere bounds, for visibility culling

// 	assert(IS_SPOT_LIGHT(L));

// 	const float half_angle = L.outer_angle;
// 	L.spot_bounds_radius = L.affect_radius * 0.5f / std::cos(half_angle);
// }

float LightManager::_surface_area_scalar(const component::RectLight &rect)
{
	return rect.size.x * rect.size.y;
}

float LightManager::_surface_area_scalar(const component::TubeLight &tube)
{
	return tube.thickness * glm::length(tube.half_extent)*2;
}

float LightManager::_surface_area_scalar(const component::SphereLight &sphere)
{
	return sphere.radius * 1.5f; // hube approximation, should probably be something with pow()
}

float LightManager::_surface_area_scalar(const component::DiscLight &disc)
{
	return disc.radius * disc.radius * std::numbers::pi_v<float>;
}

LightID LightManager::light_id(LightIndex light_index) const
{
	assert(light_index < _index_to_id.size());
	return _index_to_id[light_index];
}

LightIndex LightManager::light_index(LightID light_id) const
{
	auto found = _id_to_index.find(light_id);
	assert(found != _id_to_index.end());
	if(found == _id_to_index.end())
		return NO_LIGHT_INDEX;
	return found->second;
}

size_t RGL::LightManager::num_lights(uint_fast8_t light_type) const
{
	assert(light_type < LIGHT_TYPE__COUNT);
	if(light_type >= LIGHT_TYPE__COUNT)
		return 0;
	return _num_light_type[light_type];
}

static const glm::vec3 s_direction_reference = -AXIS_Z;

glm::quat direction_2_orientation(const glm::vec3 &direction)
{
	return glm::rotation(s_direction_reference, direction);
}

glm::vec3 orientation_2_direction(const glm::quat &orientation)
{
	return orientation * glm::vec4(s_direction_reference, 1);
}

void LightManager::create_components(LightID light_id, const PointLightParams &lp)
{
	const auto light_ent = entt::entity(light_id);

	_entities.emplace<component::Transform>(light_ent, lp.position);
	_entities.emplace<component::PointLight>(light_ent);
	// LightGeneral must be added last; it's used as trigger (see: _light_added())
	_entities.emplace<component::LightGeneral>(light_ent, component::LightGeneral::create_point(lp.color, lp.intensity, lp.shadow_caster, lp.contact_shadows, lp.fog > 0));
}

void LightManager::create_components(LightID light_id, const DirectionalLightParams &lp)
{
	const auto light_ent = entt::entity(light_id);

	_entities.emplace<component::Transform>(light_ent, component::Transform::Direction{}, lp.direction);
	_entities.emplace<component::DirectionalLight>(light_ent);
	// LightGeneral must be added last; it's used as trigger (see: _light_added())
	_entities.emplace<component::LightGeneral>(light_ent, component::LightGeneral::create_directional(lp.color, lp.intensity, lp.shadow_caster, lp.contact_shadows, lp.fog > 0));
}

void LightManager::create_components(LightID light_id, const SpotLightParams &lp)
{
	assert(lp.outer_angle >= 0 and lp.outer_angle >= lp.inner_angle);

	const auto light_ent = entt::entity(light_id);

	_entities.emplace<component::Transform>(light_ent, lp.position, component::Transform::Direction{}, lp.direction);
	_entities.emplace<component::SpotLight>(light_ent, lp.outer_angle, lp.inner_angle);
	// LightGeneral must be added last; it's used as trigger (see: _light_added())
	_entities.emplace<component::LightGeneral>(light_ent, component::LightGeneral::create_spot(lp.color, lp.intensity, lp.shadow_caster, lp.contact_shadows, lp.fog > 0));
}

void LightManager::create_components(LightID light_id, const RectLightParams &lp)
{
	const auto light_ent = entt::entity(light_id);

	_entities.emplace<component::Transform>(light_ent, lp.position, lp.orientation);
	_entities.emplace<component::RectLight>(light_ent, lp.size, lp.double_sided);
	// LightGeneral must be added last; it's used as trigger (see: _light_added())
	_entities.emplace<component::LightGeneral>(light_ent, component::LightGeneral::create_rect(lp.color, lp.intensity, lp.fog > 0));
}

void LightManager::create_components(LightID light_id, const TubeLightParams &lp)
{
	const auto light_ent = entt::entity(light_id);

	_entities.emplace<component::Transform>(light_ent, lp.position, component::Transform::Direction{}, glm::normalize(lp.half_extent));
	_entities.emplace<component::TubeLight>(light_ent, lp.half_extent, lp.thickness);
	// LightGeneral must be added last; it's used as trigger (see: _light_added())
	_entities.emplace<component::LightGeneral>(light_ent, component::LightGeneral::create_tube(lp.color, lp.intensity, lp.fog > 0));
}

void LightManager::create_components(LightID light_id, const SphereLightParams &lp)
{
	const auto light_ent = entt::entity(light_id);

	_entities.emplace<component::Transform>(light_ent, lp.position);
	_entities.emplace<component::SphereLight>(light_ent, lp.radius);
	// LightGeneral must be added last; it's used as trigger (see: _light_added())
	_entities.emplace<component::LightGeneral>(light_ent, component::LightGeneral::create_sphere(lp.color, lp.intensity, lp.fog > 0));
}

void LightManager::create_components(LightID light_id, const DiscLightParams &lp)
{
	const auto light_ent = entt::entity(light_id);

	_entities.emplace<component::Transform>(light_ent, lp.position, component::Transform::Direction{}, lp.direction);
	_entities.emplace<component::DiscLight>(light_ent, lp.radius);
	// LightGeneral must be added last; it's used as trigger (see: _light_added())
	_entities.emplace<component::LightGeneral>(light_ent, component::LightGeneral::create_disc(lp.color, lp.intensity, lp.fog > 0));
}

void LightManager::_gpu_build(LightIndex start, LightIndex end)
{
	// ScopedTimer _([start, end](auto d){
	// 	if(end > start + 1)
	// 		Log::debug("_gpu_build: lights {}-{}, in {}", start, end - 1, duration_cast<microseconds>(d));
	// 	else
	// 		Log::debug("_gpu_build: light {}, in {}", start, duration_cast<microseconds>(d));
	// });

	// this loop may be done in parallel

	for(auto light_index = start; light_index < end; ++light_index)
	{
		auto &L = _lights[light_index];

		const auto light_id = _index_to_id[light_index];
#if 0//defined(_DEBUG)
		auto Lcopy = L;
#endif

		_gpu_set_properties(L, light_id);

#if 0//defined(_DEBUG)
		if(std::memcmp(&L, &Lcopy, sizeof(L)) == 0)
			Log::debug("{{{}}} -- no GPU diff", light_id);
#endif
	}
}

float LightManager::affect_radius(LightID light_id)
{
	const auto light_ent = entt::entity(light_id);
	const auto &general =  _entities.get<component::LightGeneral>(light_ent);
	return affect_radius(light_id, general);
}

float LightManager::affect_radius(LightID light_id, const component::LightGeneral &general) const
{
	const auto light_ent = entt::entity(light_id);

	float intensity = general.intensity;
	float area = 0;

	switch(general.light_type)
	{
	case LightType::Point:
		return _intensity_to_range(general.intensity);
	case LightType::Spot:
	{
		const auto &spot = _entities.get<component::SpotLight>(light_ent);
		return affect_radius(general, spot);
	}
	break;
	case LightType::Directional:
		return std::numeric_limits<float>::max();
	case LightType::Rect:
	{
		const auto &rect = _entities.get<component::RectLight>(light_ent);
		return affect_radius(general, rect);
	}
	break;
	case LightType::Tube:
	{
		const auto &tube = _entities.get<component::TubeLight>(light_ent);
		return affect_radius(general, tube);
	}
	break;
	case LightType::Sphere:
	{
		const auto &sphere = _entities.get<component::SphereLight>(light_ent);
		return affect_radius(general, sphere);
	}
	break;
	case LightType::Disc:
	{
		const auto &disc = _entities.get<component::DiscLight>(light_ent);
		return affect_radius(general, disc);
	}
	break;
	default:
		break;
	}
	static_assert(LIGHT_TYPE__COUNT == 7);

	return _intensity_to_range(intensity, area);
}

float LightManager::affect_radius(const component::LightGeneral &general, const component::PointLight &) const
{
	return _intensity_to_range(general.intensity);
}

float LightManager::affect_radius(const component::LightGeneral &general, const component::SpotLight &spot) const
{
	float intensity = _scale_spot_intensity(general.intensity, spot);
	return _intensity_to_range(intensity);
}

float LightManager::affect_radius(const component::LightGeneral &general, const component::RectLight &rect) const
{
	float area = _surface_area_scalar(rect);
	return _intensity_to_range(general.intensity, area);
}

float LightManager::affect_radius(const component::LightGeneral &general, const component::TubeLight &tube) const
{
	float area = _surface_area_scalar(tube);
	return _intensity_to_range(general.intensity, area);
}

float LightManager::affect_radius(const component::LightGeneral &general, const component::SphereLight &sphere) const
{
	float area = _surface_area_scalar(sphere);
	return _intensity_to_range(general.intensity, area);
}

float LightManager::affect_radius(const component::LightGeneral &general, const component::DiscLight &disc) const
{
	float area = _surface_area_scalar(disc);
	return _intensity_to_range(general.intensity, area);
}

void LightManager::_gpu_set_properties(GPULight &L, LightID light_id) const
{
	const auto light_ent = entt::entity(light_id);
	const auto &[transform, general] = _entities.get<component::Transform, component::LightGeneral>(light_ent);

	L.type_flags = uint32_t(general.light_type) \
		| (general.enabled? LIGHT_ENABLED : 0) \
		| (general.shadow_caster? LIGHT_SHADOW_CASTER: 0) \
		| (general.contact_shadows? LIGHT_CONTACT_SHADOWS: 0) \
		| (general.is_volumetric? LIGHT_VOLUMETRIC: 0);

	SET_SHADOW_IDX(L, general.shadow_index); // might be LIGHT_NO_SHADOW

	L.color         = general.color;
	L.intensity     = general.intensity;
	L.fog_intensity = general.fog;

	if(general.shadow_caster)
		SET_SHADOW_COMPRESSION(L, general.shadow_compression);
	else
		SET_SHADOW_COMPRESSION(L, 0);

	if(general.light_type != LightType::Directional)
		L.position = transform.position();

	switch(general.light_type)
	{
	case LightType::Point:
	{
		L.affect_radius = _intensity_to_range(general.intensity);
	}
	break;
	case LightType::Directional:
	{
		L.direction = transform.direction();
	}
	break;
	case LightType::Spot:
	{
		L.direction = transform.direction();
		const auto &spot = _entities.get<component::SpotLight>(light_ent);
		// intensity is scaled by outer angle (smaller angle -> brighter light
		L.intensity = _scale_spot_intensity(general.intensity, spot);
		L.affect_radius = affect_radius(general, spot);
		L.outer_angle = spot.outer_angle;
		L.inner_angle = spot.inner_angle;
		L.spot_bounds_radius = L.affect_radius * 0.5f / std::cos(spot.outer_angle);
	}
	break;
	case LightType::Rect:
	{
		const auto &rect = _entities.get<component::RectLight>(light_ent);
		L.affect_radius = affect_radius(general, rect);
		_gpu_set_surface(L, transform, rect);
	}
	break;
	case LightType::Tube:
	{
		const auto &tube = _entities.get<component::TubeLight>(light_ent);
		L.affect_radius = affect_radius(general, tube);
		_gpu_set_surface(L, transform, tube);
	}
	break;
	case LightType::Sphere:
	{
		const auto &sphere = _entities.get<component::SphereLight>(light_ent);
		L.affect_radius = affect_radius(general, sphere);
		_gpu_set_surface(L, transform, sphere);
	}
	break;
	case LightType::Disc:
	{
		L.direction = transform.orientation() * glm::vec4(AXIS_X, 1);
		const auto &disc = _entities.get<component::DiscLight>(light_ent);
		L.affect_radius = affect_radius(general, disc);
		_gpu_set_surface(L, transform, disc);
	}
	break;
	}
	static_assert(LIGHT_TYPE__COUNT == 7);
}

void LightManager::_gpu_set_surface(GPULight &L, const component::Transform &transform, const component::RectLight &rect)
{
	const auto &orientation = transform.orientation();
	const glm::vec3 right   = orientation * glm::vec3(rect.size.x * 0.5f, 0,                  0);
	const glm::vec3 up      = orientation * glm::vec3(0,                  rect.size.y * 0.5f, 0);
	L.shape_data[0] = glm::vec4(+ right - up, 1);
	L.shape_data[1] = glm::vec4(- right - up, 1);
	L.shape_data[2] = glm::vec4(+ right + up, 1);
	L.shape_data[3] = glm::vec4(- right + up, 1);

	L.shape_data[4] = glm::vec4(orientation.x, orientation.y, orientation.z, orientation.w);
	L.outer_angle   = rect.size.x;
	L.inner_angle   = rect.size.y;
}

void LightManager::_gpu_set_surface(GPULight &L, const component::Transform &transform, const component::TubeLight &tube)
{
	L.shape_data[0]   = glm::vec4( tube.half_extent, 1);
	L.shape_data[1]   = glm::vec4(-tube.half_extent, 1);
	L.shape_data[2].x = tube.thickness;

	const auto &orientation = transform.orientation();
	L.shape_data[4] = glm::vec4(orientation.x, orientation.y, orientation.z, orientation.w);
	L.outer_angle = glm::length(tube.half_extent)*2;  // size
}

void LightManager::_gpu_set_surface(GPULight &L, const component::Transform &, const component::SphereLight &sphere)
{
	L.shape_data[0].x = sphere.radius;
}

void LightManager::_gpu_set_surface(GPULight &L, const component::Transform &transform, const component::DiscLight &disc)
{
	const auto &orientation = transform.orientation();
	L.shape_data[4] = glm::vec4(orientation.x, orientation.y, orientation.z, orientation.w);
	L.shape_data[0].x = disc.radius;
}

size_t LightManager::hash(const GPULight &L)
{
	size_t h { 0 };

	if(not IS_DIR_LIGHT(L))
		h = hash_combine(h, L.position);
	h = hash_combine(h, L.affect_radius); // a function of intensity (and shape)

	switch(GET_LIGHT_TYPE(L))
	{
	case LightType::Directional:
		h = hash_combine(h, L.direction);
		break;
	case LightType::Spot:
		h = hash_combine(h, L.direction);
		h = hash_combine(h, L.spot_bounds_radius);
		break;
	case LightType::Rect:
		h = hash_combine(h, L.shape_data[0]);
		h = hash_combine(h, L.shape_data[1]);
		h = hash_combine(h, L.shape_data[2]);
		h = hash_combine(h, L.shape_data[3]);
		h = hash_combine(h, L.shape_data[4]);   // orientation (quat)
		break;
	case LightType::Disc:
		h = hash_combine(h, L.shape_data[0].x); // radius
		break;
	case LightType::Tube:
		h = hash_combine(h, L.shape_data[0]);
		h = hash_combine(h, L.shape_data[1]);
		break;
	case LightType::Sphere:
		h = hash_combine(h, L.shape_data[0].x); // radius
		break;
	default:
		break;
	}
	static_assert(LIGHT_TYPE__COUNT == 7);

	return h;
}

size_t LightManager::hash(LightID light_id)
{
	const auto light_ent = entt::entity(light_id);

	const auto &[general, transform] = _entities.get<component::LightGeneral, component::Transform>(light_ent);

	return hash(light_id, general, transform);
}

size_t LightManager::hash(LightID light_id, component::LightGeneral &general, const component::Transform &transform)
{
	const auto light_ent = entt::entity(light_id);

	size_t h { 0 };
	h = hash_combine(h, general);
	h = hash_combine(h, transform);

	switch(general.light_type)
	{
	case LightType::Spot:
		h = hash_combine(h, _entities.get<component::SpotLight>(light_ent));
		break;
	case LightType::Rect:
		h = hash_combine(h, _entities.get<component::RectLight>(light_ent));
		break;
	case LightType::Tube:
		h = hash_combine(h, _entities.get<component::TubeLight>(light_ent));
		break;
	case LightType::Sphere:
		h = hash_combine(h, _entities.get<component::SphereLight>(light_ent));
		break;
	case LightType::Disc:
		h = hash_combine(h, _entities.get<component::DiscLight>(light_ent));
		break;
	default:
		// the other light-specific components are empty
		break;
	}

	return h;
}

std::string_view LightManager::type_name(const GPULight &L)
{
	return type_name(GET_LIGHT_TYPE(L));
}

std::string_view LightManager::type_name(const component::LightGeneral &general)
{
	return type_name(general.light_type);
}

std::string_view LightManager::type_name(uint_fast8_t light_type)
{
	assert(light_type < std::size(s_light_type_names));
	return s_light_type_names[light_type];
}

std::string_view LightManager::type_name(LightType light_type)
{
	return s_light_type_names[uint_fast8_t(light_type)];
}

std::expected<LightWrapper, LightError> LightManager::get_light(LightID light_id) const
{
	const auto light_ent = entt::entity(light_id);

	const auto &[general, transform] = _entities.get<component::LightGeneral, component::Transform>(light_ent);
	if(not general.enabled)
		return std::unexpected(LightError::NotEnabled);

	auto L = _lights[light_index(light_id)];

	switch(general.light_type)
	{
	case LightType::Point:
		return LightWrapper {
			general,
			_entities.get<component::PointLight>(light_ent),
			transform,
			L,
		};
	case LightType::Directional:
		return LightWrapper {
			general,
			_entities.get<component::DirectionalLight>(light_ent),
			transform,
			L,
		};
	case LightType::Spot:
		return LightWrapper {
			general,
			_entities.get<component::SpotLight>(light_ent),
			transform,
			L,
		};
	case LightType::Rect:
		return LightWrapper {
			general,
			_entities.get<component::RectLight>(light_ent),
			transform,
			L,
		};
	case LightType::Tube:
		return LightWrapper {
			general,
			_entities.get<component::TubeLight>(light_ent),
			transform,
			L,
		};
	case LightType::Sphere:
		return LightWrapper {
			general,
			_entities.get<component::SphereLight>(light_ent),
			transform,
			L,
		};
	case LightType::Disc:
		return LightWrapper {
			general,
			_entities.get<component::DiscLight>(light_ent),
			transform,
			L,
		};
	}
	return std::unexpected(LightError::NoSuchLight);
}

} // RGL
