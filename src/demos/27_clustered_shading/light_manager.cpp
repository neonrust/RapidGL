#include "light_manager.h"

#include <ranges>

#include "buffer_binds.h"
#include "light_constants.h"


using namespace std::literals;

using namespace RGL;


LightManager::LightManager(/* entt registry */) :
	_lights_ssbo("lights"sv)
{
	_lights_ssbo.setBindIndex(SSBO_BIND_LIGHTS);

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

static LightID _light_id { 0 };

PointLight LightManager::add(const PointLightParams &pd)
{
	++_light_id;  // TODO: entt EntityID

	const auto L = to_gpu_light(pd);

	add(L, _light_id);

	return to_<PointLight>(L, _light_id);
}

DirectionalLight LightManager::add(const DirectionalLightParams &d)
{
	++_light_id;  // TODO: entt EntityID

	const auto L = to_gpu_light(d);

	add(L, _light_id);

	return to_<DirectionalLight>(L, _light_id);
}

SpotLight LightManager::add(const SpotLightParams &s)
{
	++_light_id;  // TODO: entt EntityID

	const auto L = to_gpu_light(s);

	add(L, _light_id);

	return to_<SpotLight>(L, _light_id);
}

AreaLight LightManager::add(const AreaLightParams &a)
{
	++_light_id;  // TODO: entt EntityID

	const auto L = to_gpu_light(a);

	add(L, _light_id);

	return to_<AreaLight>(L, _light_id);
}

TubeLight LightManager::add(const TubeLightParams &t)
{
	++_light_id;  // TODO: entt EntityID

	const auto L = to_gpu_light(t);

	add(L, _light_id);

	return to_<TubeLight>(L, _light_id);
}

SphereLight LightManager::add(const SphereLightParams &s)
{
	++_light_id;  // TODO: entt EntityID

	const auto L = to_gpu_light(s);

	add(L, _light_id);

	return to_<SphereLight>(L, _light_id);
}

DiscLight LightManager::add(const DiscLightParams &d)
{
	++_light_id;  // TODO: entt EntityID

	const auto L = to_gpu_light(d);

	add(L, _light_id);

	return to_<DiscLight>(L, _light_id);
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
	_num_area_lights = 0;
	_num_tube_lights = 0;
	_num_sphere_lights = 0;
	_num_disc_lights = 0;
}

std::optional<GPULight> LightManager::get_by_id(LightID light_id) const
{
	auto found = _id_to_index.find(light_id);
	assert(found != _id_to_index.end());
	if(found == _id_to_index.end())
		return std::nullopt;
	return std::optional<GPULight>(_lights[found->second]);
}

std::optional<std::reference_wrapper<GPULight>> LightManager::get_by_id(LightID light_id)
{
	auto found = _id_to_index.find(light_id);
	assert(found != _id_to_index.end());
	if(found == _id_to_index.end())
		return std::nullopt;
	return _lights[found->second];
}

std::optional<std::tuple<LightID, GPULight>> LightManager::get_by_index(LightIndex light_index) const
{
	const auto &L = _lights[light_index];
	const auto found_id = _index_to_id.find(light_index);
	assert(found_id != _index_to_id.end());
	if(found_id == _index_to_id.end())
		return std::nullopt;

	const auto uuid = found_id->second;

	return std::make_tuple(uuid, L);
}

void LightManager::set(LightID uuid, const GPULight &L)
{
	auto found = _id_to_index.find(uuid);
	assert(found != _id_to_index.end());

	const auto light_index = found->second;

	_lights[light_index] = L;
	if(const auto &[_, ok] = _dirty.insert(light_index); ok)
		_dirty_list.push_back(light_index);
}

void LightManager::set(const PointLight &p)
{
	assert(p.id() != NO_LIGHT_ID);
	auto found = _id_to_index.find(p.id());
	assert(found != _id_to_index.end());

	const auto light_index = found->second;

	const auto L = to_gpu_light(p);

	// TODO: cmpare 'p' and 'L' if they actually differ?

	_lights[light_index] = L;
	if(const auto &[_, ok] =_dirty.insert(light_index); ok)
		_dirty_list.push_back(light_index);
}

void LightManager::flush()
{
	// more lights than before; upload all  (hpefully, this doesn't happen often)
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

void LightManager::add(const GPULight &L, LightID uuid)
{
	const auto next_index = LightIndex(_lights.size());

	_lights.push_back(L);
	_id_to_index[uuid] = next_index;
	_index_to_id[next_index] = uuid;
	// TODO: support contiguous ranges
	_dirty.insert(next_index);

	if(IS_POINT_LIGHT(L))
		++_num_point_lights;
	else if(IS_DIR_LIGHT(L))
		++_num_dir_lights;
	else if(IS_SPOT_LIGHT(L))
		++_num_spot_lights;
	else if(IS_AREA_LIGHT(L))
		++_num_area_lights;
	else if(IS_TUBE_LIGHT(L))
		++_num_tube_lights;
	else if(IS_SPHERE_LIGHT(L))
		++_num_sphere_lights;
	else if(IS_DISC_LIGHT(L))
		++_num_disc_lights;
}
