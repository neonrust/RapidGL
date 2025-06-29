#include "light_manager.h"

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

PointLight LightManager::add(const PointLightDef &pd)
{
	++_light_id;  // TODO: entt EntityID

	const auto L = to_gpu_light(pd);

	add(L, _light_id);

	return to_<PointLight>(L, _light_id);
}

std::optional<std::tuple<LightID, GPULight>> LightManager::get_gpu(Index light_index) const
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
	if(const auto &[_, ok] =_dirty.insert(light_index); ok)
		_dirty_list.push_back(light_index);
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
	if(const auto &[_, ok] =_dirty.insert(light_index); ok)
		_dirty_list.push_back(light_index);
}

void LightManager::flush()
{
	// more lights than before; upload all  (hpefully, this doesn't happen often)
	if(_lights.size() != _lights_ssbo.size() or _dirty.size() == _lights.size())
	{
		_lights_ssbo.set(_lights);
		std::print(stderr, "[LM] All {} lights uploaded\n", _lights.size());
	}
	else if(not _dirty.empty())
	{
		// no lights were added or removed, but some are dirty

		// make as few .update() calls to the SSBO, using contiguous ranges
		std::sort(_dirty_list.begin(), _dirty_list.end());
		Index start;
		auto last = Index(-1);

		for(auto dirty_index: _dirty_list)
		{
			if(last == Index(-1))  // new, potential range
			{
				start = dirty_index;
				last = start;
			}
			else if(dirty_index > last + 1) // index not contiguous with previous
			{
				// update the previous range (might be only one index)
				_lights_ssbo.set(_lights.begin() + start, _lights.begin() + last, start);
				start = dirty_index;
				last = Index(-1);
			}
			else
				last = dirty_index;
		}
		// the left over range at the end
		if(last == Index(-1))
			_lights_ssbo.set(_lights.begin() + start, _lights.end(), start);
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
