#include "light_manager.h"

#include <ranges>

#include "buffer_binds.h"
#include "light_constants.h"


using namespace std::literals;

using namespace RGL;

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


LightManager::LightManager(/* entt registry */) :
	_lights_ssbo("lights"sv)
{
	_lights_ssbo.bindAt(SSBO_BIND_LIGHTS);

	_dirty.reserve(1024);
	_dirty_list.reserve(1024);
	_id_to_index.reserve(1024);
	_index_to_id.reserve(1024);
}

void LightManager::reserve(size_t count)
{
	_lights_ssbo.resize(count);

	_id_to_index.reserve(count);
	_index_to_id.reserve(count);
	_dirty.reserve(count);
	_dirty_list.reserve(count);
	_lights.reserve(count);
}

void LightManager::clear()
{
	_id_to_index.clear();
	_index_to_id.clear();
	_lights.clear();
	_dirty.clear();
	_dirty_list.clear();

	_num_point_lights = 0;
	_num_dir_lights = 0;
	_num_spot_lights = 0;
	_num_rect_lights = 0;
	_num_tube_lights = 0;
	_num_sphere_lights = 0;
	_num_disc_lights = 0;
}

const GPULight &LightManager::get_by_id(LightID light_id) const
{
	auto found = _id_to_index.find(light_id);
	if(found == _id_to_index.end())
		throw std::out_of_range("id not found");

	return _lights[found->second];
}

std::tuple<LightID, const GPULight &> LightManager::at(LightIndex light_index) const
{
	assert(light_index < _lights.size());

	const auto &L = _lights[light_index];
	const auto found_id = _index_to_id.find(light_index);
	assert(found_id != _index_to_id.end());
	if(found_id == _index_to_id.end())
		throw std::out_of_range("index not found");

	const auto light_id = found_id->second;

	return std::tie(light_id, L);
}

void LightManager::set(LightID uuid, const GPULight &L)
{
	auto found = _id_to_index.find(uuid);
	assert(found != _id_to_index.end());

	const auto light_index = found->second;

	if(IS_SPOT_LIGHT(L))
	{
		auto Lspot = L;
		compute_spot_bounds(Lspot);
		_lights[light_index] = Lspot;
	}
	else
		_lights[light_index] = L;

	if(const auto &[_, ok] = _dirty.insert(light_index); ok)
		_dirty_list.push_back(light_index);
}

void LightManager::flush()
{
	// more/less lights than before; upload all  (hpefully, this doesn't happen often)
	if(_lights.size() != _lights_ssbo.size() or _dirty.size() == _lights.size())
	{
		_lights_ssbo.set(_lights);
		// std::print(stderr, "[LM] All {} lights uploaded\n", _lights.size());
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
				_lights_ssbo.set(s, _lights[s]);
			else
				_lights_ssbo.set(_lights.begin() + s, _lights.begin() + e+ 1, s);
		}
	}

	_dirty.clear();
	_dirty_list.clear();
}

void LightManager::set_shadow_index(LightID light_id, uint_fast16_t shadow_index)
{
	auto found = _id_to_index.find(light_id);
	if(found == _id_to_index.end())
		return;

	const auto light_index = found->second;

	auto &L = _lights[light_index];

	const auto prev_idx = GET_SHADOW_IDX(L);
	if(shadow_index != prev_idx)
	{
		SET_SHADOW_IDX(L, shadow_index);

		if(const auto &[_, ok] = _dirty.insert(light_index); ok)
			_dirty_list.push_back(light_index);
	}
}

void LightManager::clear_shadow_index(LightID light_id)
{
	auto found = _id_to_index.find(light_id);
	if(found == _id_to_index.end())
		return;

	const auto light_index = found->second;

	auto &L = _lights[light_index];

	const auto prev_idx = GET_SHADOW_IDX(L);
	if(prev_idx != LIGHT_NO_SHADOW)
	{
		CLR_SHADOW_IDX(L);

		if(const auto &[_, ok] = _dirty.insert(light_index); ok)
			_dirty_list.push_back(light_index);
	}
}

void LightManager::set_spot_angle(GPULight &L, float new_outer_angle)
{
	L.intensity *= spot_intensity_multiplier(new_outer_angle) / spot_intensity_multiplier(L.outer_angle);
	L.affect_radius  = std::pow(L.intensity, 0.6f);
	L.inner_angle *= new_outer_angle / L.outer_angle;
	L.outer_angle = new_outer_angle;
}

bounds::Sphere LightManager::light_bounds(const GPULight &L) const
{
	assert(not IS_DIR_LIGHT(L));
	if(IS_DIR_LIGHT(L))
		return {};

	auto bounds_center = L.position;
	auto bounds_radius = L.affect_radius;

	if(IS_SPOT_LIGHT(L))
	{
		bounds_center += L.direction * L.spot_bounds_radius;
		bounds_radius = L.spot_bounds_radius;
	}

	return bounds::Sphere(bounds_center, bounds_radius);
}

void LightManager::add(const GPULight &L, LightID light_id)
{
	const auto next_index = LightIndex(_lights.size());

	if(IS_SPOT_LIGHT(L))
	{
		auto Lspot = L;
		compute_spot_bounds(Lspot);
		_lights.push_back(Lspot);
	}
	else
		_lights.push_back(L);

	_id_to_index[light_id] = next_index;
	_index_to_id[next_index] = light_id;
	// TODO: support contiguous ranges
	_dirty.insert(next_index);

	if(IS_POINT_LIGHT(L))
		++_num_point_lights;
	else if(IS_DIR_LIGHT(L))
		++_num_dir_lights;
	else if(IS_SPOT_LIGHT(L))
		++_num_spot_lights;
	else if(IS_RECT_LIGHT(L))
		++_num_rect_lights;
	else if(IS_TUBE_LIGHT(L))
		++_num_tube_lights;
	else if(IS_SPHERE_LIGHT(L))
		++_num_sphere_lights;
	else if(IS_DISC_LIGHT(L))
		++_num_disc_lights;
}

void LightManager::compute_spot_bounds(GPULight &L)
{
	// calculate minimal sphere bounds, for visibility culling

	assert(IS_SPOT_LIGHT(L));

	const float half_angle = L.outer_angle;
	L.spot_bounds_radius = L.affect_radius * 0.5f / std::cos(half_angle);
}

LightID LightManager::light_id(LightIndex light_index) const
{
	auto found = _index_to_id.find(light_index);
	assert(found != _index_to_id.end());
	if(found == _index_to_id.end())
		return NO_LIGHT_ID;
	return found->second;
}

LightIndex LightManager::light_index(LightID light_id) const
{
	auto found = _id_to_index.find(light_id);
	assert(found != _id_to_index.end());
	if(found == _id_to_index.end())
		return NO_LIGHT_INDEX;
	return found->second;
}

float LightManager::spot_intensity_multiplier(float angle)
{
	const auto inv_cos_ref_angle = 1.f - std::cos(_private::s_spot_reference_angle);
	return inv_cos_ref_angle / (1.f - std::cos(angle));
}

std::string_view LightManager::type_name(const GPULight &L)
{
	return type_name(GET_LIGHT_TYPE(L));
}


std::string_view LightManager::type_name(uint_fast8_t light_type)
{
	assert(light_type < std::size(s_light_type_names));
	return s_light_type_names[light_type];
}
