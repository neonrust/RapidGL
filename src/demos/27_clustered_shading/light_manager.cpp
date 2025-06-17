#include "light_manager.h"

#include "buffer_binds.h"
#include "light_constants.h"


using namespace std::literals;

PointLight to_point_light(const GPULight &p, LightID uuid);


LightManager::LightManager(/* entt registry */) :
	_lights_ssbo("lights"sv)
{
	_lights_ssbo.setBindIndex(SSBO_BIND_LIGHTS);

	_dirty.reserve(1024);
	_id_to_index.reserve(1024);
	_index_to_id.reserve(1024);
}

void LightManager::reserve(size_t count)
{
	_lights_ssbo.resize(count);
}

static LightID _light_id { 0 };

PointLight LightManager::add(const PointLightDef &pd)
{
	++_light_id;  // TDO: entt EntityID

	const auto L = to_gpu_light(pd);

	add(L, _light_id);

	return to_point_light(L, _light_id);
}

std::optional<std::tuple<LightID, GPULight>> LightManager::get_gpu(index light_index) const
{
	const auto &L = _lights[light_index];
	const auto found_id = _index_to_id.find(light_index);
	assert(found_id != _index_to_id.end());
	const auto uuid = found_id->second;

	return std::make_tuple(uuid, L);
}

void LightManager::set(LightID uuid, const GPULight &L)
{
	auto found = _id_to_index.find(uuid);
	assert(found != _id_to_index.end());

	auto light_index = found->second;

	_lights[light_index] = L;
	_dirty.insert(light_index);
}

void LightManager::set(const PointLight &p)
{
	assert(p.id() != NO_LIGHT_ID);
	auto found = _id_to_index.find(p.id());
	assert(found != _id_to_index.end());

	auto light_index = found->second;

	const auto L = to_gpu_light(p);

	// TODO: cmpare 'p' and 'L' if they actually differ?

	_lights[light_index] = L;
	_dirty.insert(light_index);
}

void LightManager::flush()
{
	// more lights than before; upload all  (hpefully, this doesn't happen often)
	if(_lights.size() > _lights_ssbo.size())
	{
		_lights_ssbo.set(_lights);
		std::print(stderr, "All {} lights uploaded\n", _lights.size());
	}
	else
	{
		_lights_ssbo.resize(1 + _last_light_idx);

		// write dirty entries to the ssbo
		for(const auto list_index: _dirty)
			_lights_ssbo.set(list_index, _lights[list_index]);
	}

	// _lights_ssbo.flush();
	_dirty.clear();
}

void LightManager::add(const GPULight &L, LightID uuid)
{
	++_last_light_idx;
	assert(_last_light_idx == _lights.size());
	_lights.push_back(L);
	_id_to_index[uuid] = _last_light_idx;
	_index_to_id[_last_light_idx] = uuid;
	// TODO: support contiguous ranges
	_dirty.insert(_last_light_idx);

	switch(L.type_flags & LIGHT_TYPE_MASK)
	{
	case LIGHT_TYPE_POINT:       ++_num_point_lights; break;
	case LIGHT_TYPE_SPOT:        ++_num_spot_lights;  break;
	case LIGHT_TYPE_AREA:        ++_num_area_lights;  break;
	case LIGHT_TYPE_DIRECTIONAL: ++_num_dir_lights;   break;
	}
}


std::optional<PointLight> LightManager::to_point_light(const GPULight &L) const
{
	assert((L.type_flags & LIGHT_TYPE_MASK) == LIGHT_TYPE_POINT);
	if((L.type_flags & LIGHT_TYPE_MASK) != LIGHT_TYPE_POINT)
		return std::nullopt;

	PointLight pl;
	pl.color = L.color;
	pl.intensity = L.intensity;
	pl.fog = L.fog_intensity;
	pl.shadow_caster = (L.type_flags & LIGHT_SHADOW_CASTER) > 0;
	pl.position = L.position;
	pl.radius = L.radius;

	return pl;
}

std::optional<DirectionalLight> LightManager::to_dir_light  (const GPULight &L) const
{
	assert((L.type_flags & LIGHT_TYPE_MASK) == LIGHT_TYPE_DIRECTIONAL);
	if((L.type_flags & LIGHT_TYPE_MASK) != LIGHT_TYPE_DIRECTIONAL)
		return std::nullopt;

	// TODO

	return std::nullopt;
}

std::optional<SpotLight> LightManager::to_spot_light (const GPULight &L) const
{
	assert((L.type_flags & LIGHT_TYPE_MASK) == LIGHT_TYPE_SPOT);
	if((L.type_flags & LIGHT_TYPE_MASK) != LIGHT_TYPE_SPOT)
		return std::nullopt;

	// TODO

	return std::nullopt;
}

std::optional<AreaLight> LightManager::to_area_light (const GPULight &L) const
{
	assert((L.type_flags & LIGHT_TYPE_MASK) == LIGHT_TYPE_AREA);
	if((L.type_flags & LIGHT_TYPE_MASK) != LIGHT_TYPE_AREA)
		return std::nullopt;

	// TODO

	return std::nullopt;
}


PointLight LightManager::to_point_light(const GPULight &L, LightID uuid) const
{
	const auto found = _id_to_index.find(uuid);
	assert(found != _id_to_index.end());

	const auto list_index = found->second;

	auto pl = to_point_light(L);

	pl.value().uuid = uuid;
	pl.value().list_index = list_index;

	return pl.value();
}

GPULight LightManager::to_gpu_light(const PointLight &p)
{
	return {
		.position       = p.position,
		.type_flags     = uint32_t(LIGHT_TYPE_POINT | (p.shadow_caster? LIGHT_SHADOW_CASTER: 0)),
		.direction      = {},
		.radius         = p.radius,
		.spot_bounds_radius = 0.f,
		.intensity      = p.intensity,
		.outer_angle    = 0.f,
		.fog_intensity  = p.fog,
		.color          = p.color,
		.inner_angle    = 0.f,
		.area_points    = {},
	};
}

GPULight LightManager::to_gpu_light(const PointLightDef &p)
{
	return {
		.position       = p.position,
		.type_flags     = uint32_t(LIGHT_TYPE_POINT | (p.shadow_caster? LIGHT_SHADOW_CASTER: 0)),
		.direction      = {},
		.radius         = p.radius,
		.spot_bounds_radius = 0.f,
		.intensity      = p.intensity,
		.outer_angle    = 0.f,
		.fog_intensity  = p.fog,
		.color          = p.color,
		.inner_angle    = 0.f,
		.area_points    = {},
	};
}
