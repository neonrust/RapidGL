#include "light_manager.h"

#include <ranges>

#include "buffer_binds.h"
#include "light_constants.h"

#include "hash_combine.h"
#include "hash_vec3.h"  // IWYU pragma: keep
#include "hash_vec4.h"  // IWYU pragma: keep

using namespace std::literals;

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
		throw std::out_of_range("light id not found");

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
	set_intensity(L, L.intensity);
	L.inner_angle *= new_outer_angle / L.outer_angle;
	L.outer_angle = new_outer_angle;
}

void LightManager::set_intensity(GPULight &L, float new_intensity)
{
	L.intensity = new_intensity;

	switch(GET_LIGHT_TYPE(L))
	{
	case LIGHT_TYPE_DIRECTIONAL:
		break;
	case LIGHT_TYPE_POINT:
	case LIGHT_TYPE_SPOT:
		L.affect_radius  = std::pow(L.intensity, 0.6f);
		break;
	case LIGHT_TYPE_RECT:
	{
		const auto area = glm::distance(L.shape_data[0], L.shape_data[1]) * glm::distance(L.shape_data[2], L.shape_data[3]);
		L.affect_radius = std::pow(L.intensity, 0.6f) * (1 + area);
	}
	break;
	case LIGHT_TYPE_TUBE:
	{
		const auto area = glm::distance(L.shape_data[0], L.shape_data[1]) * L.shape_data[2].x;
		L.affect_radius = std::pow(L.intensity, 0.6f) * (1 + area);
	}
	break;
	case LIGHT_TYPE_SPHERE:
		// complete bollox
		L.affect_radius = std::pow(L.intensity, 0.6f) + L.shape_data[0].x*1.5f;
		break;
	case LIGHT_TYPE_DISC:
	{
		const float radius = L.shape_data[0].x;
		const auto area = radius * radius * std::numbers::pi_v<float>;
		L.affect_radius = std::pow(L.intensity, 0.6f) * (1 + area);
	}
	break;
	}
	static_assert(LIGHT_TYPE__COUNT == 7);
}

void LightManager::set_direction(GPULight &L, const glm::vec3 &direction)
{
	assert(IS_DIR_LIGHT(L) or IS_SPOT_LIGHT(L) or IS_DISC_LIGHT(L));

	L.direction = direction;

	if(IS_DISC_LIGHT(L))
	{
		const auto orientation = glm::rotation(glm::vec3(1, 0, 0), direction);
		L.shape_data[4] = glm::vec4(orientation.x, orientation.y, orientation.z, orientation.w);
	}
}

void LightManager::transform(GPULight &L, const glm::quat &rotate)
{
	switch(GET_LIGHT_TYPE(L))
	{
	case LIGHT_TYPE_DIRECTIONAL:
	case LIGHT_TYPE_SPOT:
	case LIGHT_TYPE_DISC:
		set_direction(L, rotate * L.direction);
		break;
	case LIGHT_TYPE_RECT:
	{
		L.shape_data[0] = rotate * L.shape_data[0];
		L.shape_data[1] = rotate * L.shape_data[1];
		L.shape_data[2] = rotate * L.shape_data[2];
		L.shape_data[3] = rotate * L.shape_data[3];
		// also update shape_data[4] (orientation)
		const auto &qvec = L.shape_data[4];
		const auto orientation = rotate * glm::quat(qvec.w, qvec.x, qvec.y, qvec.z);
		L.shape_data[4] = glm::vec4(orientation.x, orientation.y, orientation.z, orientation.w);
	}
	break;
	case LIGHT_TYPE_TUBE:
	{
		L.shape_data[0] = rotate * L.shape_data[0];
		L.shape_data[1] = rotate * L.shape_data[1];
		// also update shape_data[4] (orientation)
		const auto &qvec = L.shape_data[4];
		const auto orientation = rotate * glm::quat(qvec.w, qvec.x, qvec.y, qvec.z);
		L.shape_data[4] = glm::vec4(orientation.x, orientation.y, orientation.z, orientation.w);
	}
	break;
	default:
		// other light types makes little sense?
		assert(false);
	}
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

size_t LightManager::hash(const GPULight &L)
{
	size_t h { 0 };

	if(not IS_DIR_LIGHT(L))
		hash_combine(h, L.position);
	hash_combine(h, L.affect_radius); // a function of intensity (and shape)

	switch(GET_LIGHT_TYPE(L))
	{
	case LIGHT_TYPE_DIRECTIONAL:
		hash_combine(h, L.direction);
		break;
	case LIGHT_TYPE_SPOT:
		hash_combine(h, L.direction);
		hash_combine(h, L.spot_bounds_radius);
		break;
	case LIGHT_TYPE_RECT:
		hash_combine(h, L.shape_data[0]);
		hash_combine(h, L.shape_data[1]);
		hash_combine(h, L.shape_data[2]);
		hash_combine(h, L.shape_data[3]);
		hash_combine(h, L.shape_data[4]);   // orientation (quat)
		break;
	case LIGHT_TYPE_DISC:
		hash_combine(h, L.shape_data[0].x); // radius
		break;
	case LIGHT_TYPE_TUBE:
		hash_combine(h, L.shape_data[0]);
		hash_combine(h, L.shape_data[1]);
		break;
	case LIGHT_TYPE_SPHERE:
		hash_combine(h, L.shape_data[0].x); // radius
		break;
	}
	static_assert(LIGHT_TYPE__COUNT == 7);

	return h;
}

} // RGL
