#pragma once


#include "container_types.h"
#include "light_constants.h"
#include "lights.h"
#include "ssbo.h"

#include "generated/shared-structs.h"

#include <cstddef>

//  take a "light parameters" struct instead?
//   - returning the same type struct is a bit weird.

class LightManager
{
public:
	using LightList = std::vector<GPULight>;
	using const_iterator = LightList::const_iterator;
	using Index = uint32_t;

public:
	LightManager(/* entt registry */);

	void reserve(size_t count);

	// create light entity, return to augmented instance (e.g. with uuid set)
	//  add() or create() ?

	PointLight add(const PointLightDef &p);
	// LightID add(const SpotLight &s);
	// LightID add(const AreaLight &a);
	// LightID add(const DirectionalLight &d);

	template<typename T> requires (std::is_same_v<PointLight, T> || std::is_same_v<DirectionalLight, T> || std::is_same_v<SpotLight, T> || std::is_same_v<AreaLight, T>)
	std::optional<T> get(LightID uuid);
	std::optional<std::tuple<LightID, GPULight>> get_gpu(Index light_index) const;

	template<typename T> requires (std::is_same_v<PointLight, T> || std::is_same_v<DirectionalLight, T> || std::is_same_v<SpotLight, T> || std::is_same_v<AreaLight, T>)
	std::optional<T> at(Index list_index) const;


	void set(LightID uuid, const GPULight &L); // needs to have uuid set; sets dirty flag

	void set(const PointLight &p); // needs to have uuid set; sets dirty flag

	// update dirty lights in the SSBO
	void flush();

	inline size_t size() const { return _lights.size(); }

	inline const_iterator begin() const { return _lights.begin(); }
	inline const_iterator end()   const { return _lights.end();   }

	inline const GPULight &at(size_t index) const { return _lights.at(index); }

	inline size_t num_point_lights() const { return _num_point_lights; }
	inline size_t num_dir_lights() const   { return _num_dir_lights; }
	inline size_t num_spot_lights() const  { return _num_spot_lights; }
	inline size_t num_area_lights() const  { return _num_area_lights; }

	std::optional<PointLight>       to_point_light(const GPULight &L) const;
	std::optional<DirectionalLight> to_dir_light  (const GPULight &L) const;
	std::optional<SpotLight>        to_spot_light (const GPULight &L) const;
	std::optional<AreaLight>        to_area_light (const GPULight &L) const;

private:
	void add(const GPULight &L, LightID uuid);

	PointLight       to_point_light(const GPULight &L, LightID uuid) const;
	DirectionalLight to_dir_light  (const GPULight &L, LightID uuid) const;
	SpotLight        to_spot_light (const GPULight &L, LightID uuid) const;
	AreaLight        to_area_light (const GPULight &L, LightID uuid) const;

	static GPULight to_gpu_light(const PointLight &p);
	static GPULight to_gpu_light(const PointLightDef &p);

private:

	dense_map<LightID, Index> _id_to_index;
	dense_map<Index, LightID> _index_to_id;
	// TODO: support contiguous ranges
	dense_set<Index> _dirty;
	std::vector<Index> _dirty_list;
	// essentially a CPU-local mirror of the SSBO
	LightList _lights;

	RGL::buffer::ShaderStorage<GPULight> _lights_ssbo;
	Index _last_light_idx { Index(-1) };

	size_t _num_point_lights { 0 };
	size_t _num_dir_lights { 0 };
	size_t _num_spot_lights { 0 };
	size_t _num_area_lights { 0 };
};

template<typename T> requires (std::is_same_v<PointLight, T> || std::is_same_v<DirectionalLight, T> || std::is_same_v<SpotLight, T> || std::is_same_v<AreaLight, T>)
inline std::optional<T> LightManager::at(Index light_index) const
{
	assert(light_index < _lights.size());

	const auto &light = _lights[light_index];
	const auto found_id = _index_to_id.find(light_index);
	assert(found_id != _index_to_id.end());
	const auto uuid = found_id->second;

	if constexpr (std::is_same_v<T, PointLight>)
		return to_point_light(light, uuid);
	static_assert(false);
}

template<typename T> requires (std::is_same_v<PointLight, T> || std::is_same_v<DirectionalLight, T> || std::is_same_v<SpotLight, T> || std::is_same_v<AreaLight, T>)
inline std::optional<T> LightManager::get(LightID uuid)
{
	const auto found = _id_to_index.find(uuid);
	assert(found != _id_to_index.end());

	const auto index = found->second;
	assert(index < _lights.size());

	const auto &L = _lights[index];

	if constexpr (std::is_same_v<PointLight, T>)
	{
		assert((L.type_flags & LIGHT_TYPE_POINT) > 0);
		return to_point_light(L, uuid);
	}
	else if constexpr (std::is_same_v<DirectionalLight, T>)
	{
		assert((L.type_flags & LIGHT_TYPE_DIRECTIONAL) > 0);
		return to_dir_light(L, uuid);
	}
	if constexpr (std::is_same_v<SpotLight, T>)
	{
		assert((L.type_flags & LIGHT_TYPE_SPOT) > 0);
		return to_spot_light(L, uuid);
	}
	assert((L.type_flags & LIGHT_TYPE_AREA) > 0);
	return to_area_light(L, uuid);
}
