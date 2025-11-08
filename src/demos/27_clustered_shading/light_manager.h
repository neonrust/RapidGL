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

// create "mapping" from type XParams -> type X
template<typename T>
struct light_map;
#define MAP_PARAMS_TYPE(LT) template <> struct light_map<LT ## Params>  { using type = LT; }
MAP_PARAMS_TYPE(PointLight);
MAP_PARAMS_TYPE(DirectionalLight);
MAP_PARAMS_TYPE(SpotLight);
MAP_PARAMS_TYPE(AreaLight);
MAP_PARAMS_TYPE(TubeLight);
MAP_PARAMS_TYPE(SphereLight);
MAP_PARAMS_TYPE(DiscLight);

template <typename LTP>
using return_type = typename light_map<LTP>::type;

static constexpr float s_spot_reference_angle = glm::radians(45.f);

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

	template<_private::LightParamsType LTP>
	auto add(const LTP &ltp) -> _private::return_type<LTP>;

	// PointLight    add(const PointLightParams &p);
	// DirectionalLight add(const DirectionalLightParams &d);
	// SpotLight     add(const SpotLightParams &s);
	// AreaLight     add(const AreaLightParams &a);
	// TubeLight     add(const TubeLightParams &t);
	// SphereLight   add(const SphereLightParams &s);
	// DiscLight     add(const DiscLightParams &d);

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

	static void set_spot_angle(GPULight &L, float new_outer_angle);

	bounds::Sphere light_bounds(const GPULight &L) const;

	template<_private::LightType LT>
	static std::string_view type_name();
	static std::string_view type_name(const GPULight &L);

private:
	void add(const GPULight &L, LightID light_id);

	// convert any light type to a GPULight
	template<typename LT> requires _private::LightType<LT> || _private::LightParamsType<LT>
	static GPULight to_gpu_light(const LT &l);
	static void compute_spot_bounds(GPULight &L);
	static float spot_intensity_multiplier(float angle);

	// convert an XParams light type to its corresponding "handle" type (includes LightID)
	template<_private::LightParamsType LTP>
	auto to_typed(const LTP &lpt, LightID light_id) const -> _private::return_type<LTP>;

	static std::string_view type_name(uint_fast8_t light_type);

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

	_lights[light_index] = L;

	// TODO: compare 'L' and '_lights[light_index]' if they actually differ?
	if(const auto &[_, ok] =_dirty.insert(light_index); ok)
		_dirty_list.push_back(light_index);
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

	return to_typed<LT>(light, uuid);
}

template<_private::LightType LT>
std::optional<LT> LightManager::get(LightID light_id)
{
	const auto found = _id_to_index.find(light_id);
	assert(found != _id_to_index.end());

	const auto index = found->second;
	assert(index < _lights.size());

	const auto &L = _lights[index];

	return to_typed<LT>(L, light_id);
}

namespace
{
static LightID s_light_id { 0 };
}

template<_private::LightParamsType LTP>
auto LightManager::add(const LTP &ltp) -> _private::return_type<LTP>
{
	++s_light_id;

	assert(not _id_to_index.contains(s_light_id));

	auto L = to_gpu_light(ltp);

	add(L, s_light_id);

	return to_typed(ltp, s_light_id);
}

template<typename LT> requires _private::LightType<LT> || _private::LightParamsType<LT>
GPULight LightManager::to_gpu_light(const LT &l)
{
	GPULight L;
	L.color         = l.color;
	L.intensity     = l.intensity;
	L.fog_intensity = l.fog;

	if constexpr (not std::same_as<LT, DirectionalLight> or std::same_as<LT, DirectionalLightParams>)
	{
		L.position      = l.position;
	}

	if constexpr (std::same_as<LT, PointLight> or std::same_as<LT, PointLightParams>)
	{
		L.type_flags    = LIGHT_TYPE_POINT;
		L.affect_radius = std::pow(l.intensity, 0.6f);
	}
	else if constexpr (std::same_as<LT, DirectionalLight> or std::same_as<LT, DirectionalLightParams>)
	{
		L.type_flags    = LIGHT_TYPE_DIRECTIONAL | (l.fog > 0? LIGHT_VOLUMETRIC: 0);
		L.direction     = l.direction;
	}
	else if constexpr (std::same_as<LT, SpotLight> or std::same_as<LT, SpotLightParams>)
	{
		L.type_flags     = LIGHT_TYPE_SPOT;
		L.direction      = l.direction;
		L.outer_angle    = _private::s_spot_reference_angle;
		assert(l.outer_angle >= l.inner_angle);
		const auto inner_angle = std::min(l.inner_angle, l.outer_angle);
		L.inner_angle    = (inner_angle / l.outer_angle) * L.outer_angle;

		set_spot_angle(L, l.outer_angle);
		compute_spot_bounds(L);
	}
	else if constexpr (std::same_as<LT, AreaLight> or std::same_as<LT, AreaLightParams>)
	{
		L.type_flags      = LIGHT_TYPE_AREA | (l.double_sided? LIGHT_DOUBLE_SIDED: 0);
		glm::vec3 right   = l.orientation * glm::vec3(l.size.x * 0.5f, 0,               0);
		glm::vec3 up      = l.orientation * glm::vec3(0,               l.size.y * 0.5f, 0);
		L.shape_points[0] = glm::vec4(l.position + right - up, 1);
		L.shape_points[1] = glm::vec4(l.position - right - up, 1);
		L.shape_points[2] = glm::vec4(l.position + right + up, 1);
		L.shape_points[3] = glm::vec4(l.position - right + up, 1);
		// TODO: this is MASSIVELY over estimating the radius
		L.affect_radius   = 50 * l.intensity * glm::distance(l.position, glm::vec3(L.shape_points[1]));
	}
	else if constexpr (std::same_as<LT, TubeLight> or std::same_as<LT, TubeLightParams>)
	{
		L.type_flags        = LIGHT_TYPE_TUBE;
		L.shape_points[0]   = glm::vec4(l.end_points[0], 1);
		L.shape_points[1]   = glm::vec4(l.end_points[1], 1);
		L.shape_points[2].x = l.thickness;
		// L.affect_radius     =      TODO
	}
	else if constexpr (std::same_as<LT, SphereLight> or std::same_as<LT, SphereLightParams>)
	{
		L.type_flags        = LIGHT_TYPE_SPHERE;
		L.shape_points[0].x = l.radius;
		// L.affect_radius     =       TODO
	}
	else if constexpr (std::same_as<LT, DiscLight> or std::same_as<LT, DiscLightParams>)
	{
		L.type_flags        = LIGHT_TYPE_DISC | (l.double_sided? LIGHT_DOUBLE_SIDED: 0);
		L.direction         = l.direction;
		L.shape_points[0].x = l.radius;
		// L.affect_radius     =       TODO
	}

	if(l.shadow_caster)
		L.type_flags |= LIGHT_SHADOW_CASTER;
	if(l.fog > 0)
		L.type_flags |= LIGHT_VOLUMETRIC;

	CLR_SHADOW_IDX(L);

	return L;
}

template<_private::LightParamsType LTP>
auto LightManager::to_typed(const LTP &ltp, LightID light_id) const -> _private::return_type<LTP>
{
	using LT = _private::return_type<LTP>;

	LT lt;
	lt.color      = ltp.color;
	lt.intensity  = ltp.intensity;
	lt.fog        = ltp.fog;
	lt.uuid       = light_id;

	if constexpr (std::same_as<LTP, PointLightParams>)
	{
		lt.position = ltp.position;
	}
	else if constexpr (std::same_as<LTP, DirectionalLightParams>)
	{
		lt.direction = ltp.direction;
	}
	else if constexpr (std::same_as<LTP, SpotLightParams>)
	{
		lt.position    = ltp.position;
		lt.direction   = ltp.direction;
		lt.outer_angle = ltp.outer_angle;
		lt.inner_angle = ltp.inner_angle;
	}
	else if constexpr (std::same_as<LTP, AreaLightParams>)
	{
		lt.position     = ltp.position;
		lt.orientation  = ltp.orientation;
		lt.size         = ltp.size;
		lt.double_sided = ltp.double_sided;
	}
	else if constexpr (std::same_as<LTP, TubeLightParams>)
	{
		lt.end_points   = ltp.end_points;
		lt.thickness    = ltp.thickness;
	}
	else if constexpr (std::same_as<LTP, SphereLightParams>)
	{
		lt.position     = ltp.position;
		lt.radius       = ltp.radius;
	}
	else if constexpr (std::same_as<LTP, DiscLightParams>)
	{
		lt.position    = ltp.position;
		lt.direction   = ltp.direction;
	}

	return lt;
}

template<_private::LightType LT>
std::string_view LightManager::type_name()
{
	if constexpr (std::is_same_v<LT, PointLight>)
		return type_name(LIGHT_TYPE_POINT);
	if constexpr (std::is_same_v<LT, DirectionalLight>)
		return type_name(LIGHT_TYPE_DIRECTIONAL);
	if constexpr (std::is_same_v<LT, SpotLight>)
		return type_name(LIGHT_TYPE_SPOT);
	if constexpr (std::is_same_v<LT, AreaLight>)
		return type_name(LIGHT_TYPE_AREA);
	if constexpr (std::is_same_v<LT, TubeLight>)
		return type_name(LIGHT_TYPE_TUBE);
	if constexpr (std::is_same_v<LT, SphereLight>)
		return type_name(LIGHT_TYPE_SPHERE);
	if constexpr (std::is_same_v<LT, DiscLight>)
		return type_name(LIGHT_TYPE_DISC);
	return std::string_view("{unknown}");
}
