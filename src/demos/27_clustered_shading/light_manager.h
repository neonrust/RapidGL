#pragma once


#include "bounds.h"
#include "container_types.h"
#include "light_constants.h"
#include "lights.h"
#include "ssbo.h"

#include "generated/shared-structs.h"

#include <cstddef>


namespace _private
{
template<typename T>
concept LightType = std::is_same_v<PointLight, T>
	or std::is_same_v<DirectionalLight, T>
	or std::is_same_v<SpotLight, T>
	or std::is_same_v<AreaLight, T>
	or std::is_same_v<TubeLight, T>
	or std::is_same_v<SphereLight, T>
	or std::is_same_v<DiscLight, T>
	;

template<typename T>
concept LightParamsType = std::is_same_v<PointLightParams, T>
	or std::is_same_v<DirectionalLightParams, T>
	or std::is_same_v<SpotLightParams, T>
	or std::is_same_v<AreaLightParams, T>
	or std::is_same_v<TubeLightParams, T>
	or std::is_same_v<SphereLightParams, T>
	or std::is_same_v<DiscLightParams, T>
	;
} // _private


// TODO: not entirely happy with this API ...
//   the tricky part is that we need both index-based and ID-based access.

class LightManager
{
public:
	using LightList = std::vector<GPULight>;
	// TODO: using LightList = std::vector<std::pair<LightID, GPULight>>;
	using const_iterator = LightList::const_iterator;

public:
	LightManager(/* entt registry */);

	void reserve(size_t count);

	// create light entity, return to augmented instance (e.g. with uuid set)
	//  add() or create() ?

	std::string_view type_name(const GPULight &L) const;

	PointLight    add(const PointLightParams &p);
	DirectionalLight add(const DirectionalLightParams &d);
	SpotLight     add(const SpotLightParams &s);
	AreaLight     add(const AreaLightParams &a);
	TubeLight     add(const TubeLightParams &t);
	SphereLight   add(const SphereLightParams &s);
	DiscLight     add(const DiscLightParams &d);

	void clear();

	template<_private::LightType LT>
	std::optional<LT> get(LightID uuid);

	const GPULight &get_by_id(LightID light_id) const ;

	std::tuple<LightID, const GPULight &> at(LightIndex light_index) const;
	template<_private::LightType LT>
	LT at(LightIndex list_index) const;

	inline const GPULight &operator [] (LightIndex light_index) const noexcept { return _lights[light_index]; }

	void set(LightID uuid, const GPULight &L); // sets dirty flag

	template<_private::LightType LT>
	void set(const LT &l); // needs to have uuid set; sets dirty flag
	// TODO: set() for other light types

	// update dirty lights in the SSBO
	void flush();

	inline size_t size() const { return _lights.size(); }

	inline const_iterator cbegin() const { return _lights.cbegin(); }
	inline const_iterator cend()   const { return _lights.cend();   }
	inline const_iterator  begin() const { return cbegin(); }
	inline const_iterator  end()   const { return cend();   }

	LightID light_id(LightIndex light_index) const;
	LightIndex light_index(LightID light_id) const;
	inline const GPULight &at(size_t light_index) const { return _lights.at(light_index); }

	inline uint_fast16_t shadow_index(LightID light_id) const { return GET_SHADOW_IDX(get_by_id(light_id)); }
	void set_shadow_index(LightID light_id, uint_fast16_t shadow_index);
	void clear_shadow_index(LightID light_id);

	template<typename LT=GPULight> requires (std::same_as<LT, GPULight> || _private::LightType<LT>)
	inline size_t num_lights() const;

	template<_private::LightType LT>
	std::optional<LT> to_(const GPULight &L) const;

	bounds::Sphere light_bounds(const GPULight &L) const;

private:
	void add(const GPULight &L, LightID light_id);
	void compute_spot_bounds(GPULight &L);

	template<_private::LightType LT>
	LT to_(const GPULight &L, LightID light_id) const;

	template<typename LT> requires _private::LightType<LT> || _private::LightParamsType<LT>
	static GPULight to_gpu_light(const LT &l);

private:
	dense_map<LightID, LightIndex> _id_to_index;
	dense_map<LightIndex, LightID> _index_to_id; // TODO: can this be a vector? store directly in '_lights'?

	dense_set<LightIndex> _dirty;
	std::vector<LightIndex> _dirty_list;
	// essentially a CPU-side mirror of the SSBO  (otherwise we'd use a mapping container)
	LightList _lights;

	RGL::buffer::Storage<GPULight> _lights_ssbo;

	size_t _num_point_lights  { 0 };
	size_t _num_dir_lights    { 0 };
	size_t _num_spot_lights   { 0 };
	size_t _num_area_lights   { 0 };
	size_t _num_tube_lights   { 0 };
	size_t _num_sphere_lights { 0 };
	size_t _num_disc_lights   { 0 };
};

template<_private::LightType LT>
inline void LightManager::set(const LT &l)
{
	assert(l.id() != NO_LIGHT_ID);
	auto found = _id_to_index.find(l.id());
	assert(found != _id_to_index.end());

	const auto L = to_gpu_light(l);

	const auto light_index = found->second;

#if defined(DEBUG)
	if constexpr (std::same_as<LT, PointLight>)
		assert(IS_POINT_LIGHT(_lights[light_index]));
	else if constexpr (std::same_as<LT, DirectionalLight>)
		assert(IS_DIR_LIGHT(_lights[light_index]));
	else if constexpr (std::same_as<LT, SpotLight>)
		assert(IS_SPOT_LIGHT(_lights[light_index]));
	else if constexpr (std::same_as<LT, AreaLight>)
		assert(IS_AREA_LIGHT(_lights[light_index]));
	else if constexpr (std::same_as<LT, TubeLight>)
		assert(IS_TUBE_LIGHT(_lights[light_index]));
	else if constexpr (std::same_as<LT, SphereLight>)
		assert(IS_SPHERE_LIGHT(_lights[light_index]));
	else if constexpr (std::same_as<LT, DiscLight>)
		assert(IS_DISC_LIGHT(_lights[light_index]));
#endif

	_lights[light_index] = L;

	// TODO: compare 'L' and '_lights[light_index]' if they actually differ?
	if(const auto &[_, ok] =_dirty.insert(light_index); ok)
		_dirty_list.push_back(light_index);
}

template<_private::LightType LT>
LT LightManager::to_(const GPULight &L, LightID light_id) const
{
	const auto found = _id_to_index.find(light_id);
	assert(found != _id_to_index.end());

	const auto list_index = found->second;

	auto l = to_<LT>(L).value();

	l.uuid = light_id;
	l.list_index = list_index;

	return l;
}

template<typename LT>  requires (std::same_as<LT, GPULight> || _private::LightType<LT>)
size_t LightManager::num_lights() const
{
	if constexpr (std::same_as<LT, GPULight>)
		return _lights.size();
	else if constexpr (std::same_as<LT, PointLight>)
		return _num_point_lights;
	else if constexpr (std::same_as<LT, DirectionalLight>)
		return _num_dir_lights;
	else if constexpr (std::same_as<LT, SpotLight>)
		return _num_spot_lights;
	else if constexpr (std::same_as<LT, AreaLight>)
		return _num_area_lights;
	else if constexpr (std::same_as<LT, TubeLight>)
		return _num_tube_lights;
	else if constexpr (std::same_as<LT, SphereLight>)
		return _num_sphere_lights;
	else if constexpr (std::same_as<LT, DiscLight>)
		return _num_disc_lights;
}

template<_private::LightType LT>
LT LightManager::at(LightIndex light_index) const
{
	const auto &light = _lights[light_index];
	const auto found_id = _index_to_id.find(light_index);
	assert(found_id != _index_to_id.end());

	const auto uuid = found_id->second;

	return to_<LT>(light, uuid);
}

template<_private::LightType LT>
std::optional<LT> LightManager::get(LightID light_id)
{
	const auto found = _id_to_index.find(light_id);
	assert(found != _id_to_index.end());

	const auto index = found->second;
	assert(index < _lights.size());

	const auto &L = _lights[index];

	return to_<LT>(L, light_id);
}

template<_private::LightType LT>
std::optional<LT> LightManager::to_(const GPULight &L) const
{
	LT l;
	l.color         = L.color;
	l.intensity     = L.intensity;
	l.affect_radius = L.affect_radius;
	l.fog           = L.fog_intensity;
	l.shadow_caster = IS_SHADOW_CASTER(L);

	if constexpr (std::same_as<LT, PointLight>)
	{
		assert(IS_POINT_LIGHT(L));
		if(not IS_POINT_LIGHT(L))
		{
			std::print(stderr, "Expected point light, got {}\n", type_name(L));
			return std::nullopt;
		}

		l.position      = L.position;
		l.affect_radius = L.affect_radius;
	}
	else if constexpr (std::same_as<LT, DirectionalLight>)
	{
		assert(IS_DIR_LIGHT(L));
		if(not IS_DIR_LIGHT(L))
		{
			std::print(stderr, "Expected directional light, got {}\n", type_name(L));
			return std::nullopt;
		}

		l.direction = L.direction;
	}
	else if constexpr (std::same_as<LT, SpotLight>)
	{
		assert(IS_SPOT_LIGHT(L));
		if(not IS_SPOT_LIGHT(L))
		{
			std::print(stderr, "Expected spot light, got {}\n", type_name(L));
			return std::nullopt;
		}

		l.position      = L.position;
		l.direction     = L.direction;
		l.outer_angle   = L.outer_angle;
		l.inner_angle   = L.inner_angle;
		l.bounds_radius = L.spot_bounds_radius;
	}
	else if constexpr (std::same_as<LT, AreaLight>)
	{
		assert(IS_AREA_LIGHT(L));
		if(not IS_AREA_LIGHT(L))
		{
			std::print(stderr, "Expected area light, got {}\n", type_name(L));
			return std::nullopt;
		}

		l.points[0] = L.shape_points[0];
		l.points[1] = L.shape_points[1];
		l.points[2] = L.shape_points[2];
		l.points[3] = L.shape_points[3];
		l.two_sided = (L.type_flags & LIGHT_TWO_SIDED) > 0;
	}
	else if constexpr (std::same_as<LT, TubeLight>)
	{
		assert(IS_TUBE_LIGHT(L));
		if(not IS_TUBE_LIGHT(L))
		{
			std::print(stderr, "Expected tube light, got {}\n", type_name(L));
			return std::nullopt;
		}

		l.end_points[0] = L.shape_points[0];
		l.end_points[1] = L.shape_points[1];
		l.thickness     = L.shape_points[2].x;
	}
	else if constexpr (std::same_as<LT, SphereLight>)
	{
		assert(IS_SPHERE_LIGHT(L));
		if(not IS_SPHERE_LIGHT(L))
		{
			std::print(stderr, "Expected sphere light, got {}\n", type_name(L));
			return std::nullopt;
		}

		l.sphere_radius = L.shape_points[0].x;
	}
	else if constexpr (std::same_as<LT, DiscLight>)
	{
		assert(IS_DISC_LIGHT(L));
		if(not IS_DISC_LIGHT(L))
		{
			std::print(stderr, "Expected disc light, got {}\n", type_name(L));
			return std::nullopt;
		}

		l.position    = L.position;
		l.direction   = L.direction;
		l.disc_radius = L.shape_points[0].x;
	}

	return l;
}

template<typename LT> requires _private::LightType<LT> || _private::LightParamsType<LT>
GPULight LightManager::to_gpu_light(const LT &l)
{
	GPULight L;
	L.color         = l.color;
	L.intensity     = l.intensity;
	L.fog_intensity = l.fog;
	L.affect_radius = l.affect_radius;

	if constexpr (std::same_as<LT, PointLight> or std::same_as<LT, PointLightParams>)
	{
		L.type_flags    = LIGHT_TYPE_POINT | (l.shadow_caster? LIGHT_SHADOW_CASTER: 0);
		L.position      = l.position;
	}
	else if constexpr (std::same_as<LT, DirectionalLight> or std::same_as<LT, DirectionalLightParams>)
	{
		L.type_flags    = LIGHT_TYPE_DIRECTIONAL;
		L.direction     = l.direction;
	}
	else if constexpr (std::same_as<LT, SpotLight> or std::same_as<LT, SpotLightParams>)
	{
		L.type_flags     = LIGHT_TYPE_SPOT | (l.shadow_caster? LIGHT_SHADOW_CASTER: 0);
		L.position       = l.position;
		L.direction      = l.direction;
		L.outer_angle    = l.outer_angle;
		L.inner_angle    = l.inner_angle;
	}
	else if constexpr (std::same_as<LT, AreaLight> or std::same_as<LT, AreaLightParams>)
	{
		L.type_flags      = LIGHT_TYPE_AREA | (l.two_sided? LIGHT_TWO_SIDED: 0);
		L.shape_points[0] = l.points[0];
		L.shape_points[1] = l.points[1];
		L.shape_points[2] = l.points[2];
		L.shape_points[3] = l.points[3];
	}
	else if constexpr (std::same_as<LT, TubeLight> or std::same_as<LT, TubeLightParams>)
	{
		L.type_flags        = LIGHT_TYPE_TUBE;
		L.shape_points[0]   = l.end_points[0];
		L.shape_points[1]   = l.end_points[1];
		L.shape_points[2].x = l.thickness;
	}
	else if constexpr (std::same_as<LT, SphereLight> or std::same_as<LT, SphereLightParams>)
	{
		L.type_flags        = LIGHT_TYPE_SPHERE;
		L.shape_points[0].x = l.sphere_radius;
	}
	else if constexpr (std::same_as<LT, DiscLight> or std::same_as<LT, DiscLightParams>)
	{
		L.type_flags        = LIGHT_TYPE_DISC;
		L.position          = l.position;
		L.direction         = l.direction;
		L.shape_points[0].x = l.disc_radius;
	}

	CLR_SHADOW_IDX(L);

	return L;
}
