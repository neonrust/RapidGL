#pragma once


#include "bounds.h"
#include "container_types.h"
#include "light_constants.h"
#include "lights.h"
#include "ssbo.h"

#include "generated/shared-structs.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <cstddef>


// TODO: 'lights' namespace
//   lights::Manager
//   lights::set_*(GPULight &L, ...)
//   lights::transform(GPULight &L, ...)

namespace RGL
{

namespace _private
{
template<typename T>
concept LightType = std::is_same_v<PointLight, T>
	or std::is_same_v<DirectionalLight, T>
	or std::is_same_v<SpotLight, T>
	or std::is_same_v<RectLight, T>
	or std::is_same_v<TubeLight, T>
	or std::is_same_v<SphereLight, T>
	or std::is_same_v<DiscLight, T>
	;

template<typename T>
concept LightParamsType = std::is_same_v<PointLightParams, T>
	or std::is_same_v<DirectionalLightParams, T>
	or std::is_same_v<SpotLightParams, T>
	or std::is_same_v<RectLightParams, T>
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
MAP_PARAMS_TYPE(RectLight);
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
	LightManager(/* ecs registry */);

	void reserve(size_t count);

	// create light entity, return to augmented instance (e.g. with uuid set)
	//  add() or create() ?

	template<_private::LightParamsType LTP>
	auto add(const LTP &ltp) -> _private::return_type<LTP>;

	inline void set_falloff_power(float power=1) { _falloff_power = power; }
	inline void set_radius_power(float power=0.6f) { _radius_power = power; }
	inline float falloff_power() const { return _falloff_power; }

	void clear();

	// template<_private::LightType LT>
	// std::optional<LT> get(LightID uuid);

	const GPULight &get_by_id(LightID light_id) const;

	std::tuple<LightID, const GPULight &> at(LightIndex light_index) const;
	// template<_private::LightType LT>
	// LT typed_at(LightIndex list_index) const;

	inline const GPULight &operator [] (LightIndex light_index) const noexcept { return _lights[light_index]; }

	void set(LightID uuid, const GPULight &L); // sets dirty flag

	template<_private::LightType LT>
	void set(const LT &l); // needs to have uuid set; sets dirty flag

	inline bool contains(LightID light_id) const { return _id_to_index.contains(light_id); }

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

	void set_intensity(GPULight &L, float new_intensity) const;  // sets intensity and affect_radius
	void set_spot_angle(GPULight &L, float new_outer_angle) const; // also sets intensity and affect_radius
	static void set_direction(GPULight &L, const glm::vec3 &direction);  // spot & disc lights
	static void transform(GPULight &L, const glm::mat4 &tfm);
	static void transform(GPULight &L, const glm::mat3 &rotate);
	static void transform(GPULight &L, const glm::quat &rotate);

	bool is_enabled(LightID light_id) const;
	void set_enabled(LightID light_id, bool enabled=true);

	// calculate a hash of its properties
	size_t hash(const GPULight &L);
	bounds::Sphere light_bounds(const GPULight &L) const;

	template<_private::LightType LT>
	static std::string_view type_name();
	static std::string_view type_name(const GPULight &L);
	static std::string_view type_name(uint_fast8_t light_type);

private:
	void add(const GPULight &L, LightID light_id);

	// convert any light type to a GPULight
	template<typename LT> requires _private::LightType<LT> || _private::LightParamsType<LT>
	GPULight to_gpu_light(const LT &l) const;
	static void compute_spot_bounds(GPULight &L);
	static float spot_intensity_multiplier(float angle);

	// convert an XParams light type to its corresponding "handle" type (includes LightID)
	template<_private::LightParamsType LTP>
	auto to_typed(const LTP &lpt, LightID light_id) const -> _private::return_type<LTP>;

private:
	dense_map<LightID, LightIndex> _id_to_index;
	dense_map<LightIndex, LightID> _index_to_id; // TODO: can this be a vector? store directly in '_lights'?

	dense_set<LightIndex> _dirty;
	std::vector<LightIndex> _dirty_list;
	// essentially a CPU-side mirror of the SSBO  (otherwise we'd use a mapping container)
	LightList _lights;

	buffer::Storage<GPULight> _lights_ssbo;

	float _radius_power  { 0.6f };
	float _falloff_power { 50.f };

	size_t _num_point_lights  { 0 };
	size_t _num_dir_lights    { 0 };
	size_t _num_spot_lights   { 0 };
	size_t _num_rect_lights   { 0 };
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
	else if constexpr (std::same_as<LT, RectLight>)
		return _num_rect_lights;
	else if constexpr (std::same_as<LT, TubeLight>)
		return _num_tube_lights;
	else if constexpr (std::same_as<LT, SphereLight>)
		return _num_sphere_lights;
	else if constexpr (std::same_as<LT, DiscLight>)
		return _num_disc_lights;
}

// template<_private::LightType LT>
// LT LightManager::typed_at(LightIndex light_index) const
// {
// 	const auto &light = _lights[light_index];
// 	const auto found_id = _index_to_id.find(light_index);
// 	assert(found_id != _index_to_id.end());

// 	const auto uuid = found_id->second;

// 	return to_typed<LT>(light, uuid);
// }

// template<_private::LightType LT>
// std::optional<LT> LightManager::get(LightID light_id)
// {
// 	const auto found = _id_to_index.find(light_id);
// 	assert(found != _id_to_index.end());

// 	const auto index = found->second;
// 	assert(index < _lights.size());

// 	const auto &L = _lights[index];

// 	return to_typed<LT>(L, light_id);
// }

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
GPULight LightManager::to_gpu_light(const LT &l) const
{
	GPULight L;
	L.color         = l.color;
	L.fog_intensity = l.fog;

	static_assert(LIGHT_TYPE__COUNT == 7);

	// all lights, except directional, has a position
	if constexpr (not (std::same_as<LT, DirectionalLight> or std::same_as<LT, DirectionalLightParams>))
		L.position      = l.position;

	if constexpr (std::same_as<LT, PointLight> or std::same_as<LT, PointLightParams>)
	{
		L.type_flags    = LIGHT_TYPE_POINT;
	}
	else if constexpr (std::same_as<LT, DirectionalLight> or std::same_as<LT, DirectionalLightParams>)
	{
		L.type_flags    = LIGHT_TYPE_DIRECTIONAL | (l.fog > 0? LIGHT_VOLUMETRIC: 0);
		L.direction     = l.direction;
	}
	else if constexpr (std::same_as<LT, SpotLight> or std::same_as<LT, SpotLightParams>)
	{
		L.type_flags    = LIGHT_TYPE_SPOT;
		set_direction(L, l.direction);
		L.outer_angle   = _private::s_spot_reference_angle;
		assert(l.outer_angle >= l.inner_angle);
		const auto inner_angle = std::min(l.inner_angle, l.outer_angle);
		L.inner_angle   = (inner_angle / l.outer_angle) * L.outer_angle;

		set_spot_angle(L, l.outer_angle);
		compute_spot_bounds(L);
	}
	else if constexpr (std::same_as<LT, RectLight> or std::same_as<LT, RectLightParams>)
	{
		L.type_flags    = LIGHT_TYPE_RECT | (l.double_sided? LIGHT_DOUBLE_SIDED: 0) \
			| (l.visible_surface? LIGHT_VISIBLE_SURFACE : 0);
		const glm::vec3 right   = l.orientation * glm::vec3(l.size.x * 0.5f, 0,               0);
		const glm::vec3 up      = l.orientation * glm::vec3(0,               l.size.y * 0.5f, 0);
		L.shape_data[0] = glm::vec4(+ right - up, 1);
		L.shape_data[1] = glm::vec4(- right - up, 1);
		L.shape_data[2] = glm::vec4(+ right + up, 1);
		L.shape_data[3] = glm::vec4(- right + up, 1);

		L.shape_data[4] = glm::vec4(l.orientation.x, l.orientation.y, l.orientation.z, l.orientation.w);
		L.outer_angle   = l.size.x;
		L.inner_angle   = l.size.y;
	}
	else if constexpr (std::same_as<LT, TubeLight> or std::same_as<LT, TubeLightParams>)
	{
		assert(l.thickness < glm::length(l.half_extent)/2);

		L.type_flags      = LIGHT_TYPE_TUBE \
			| (l.visible_surface? LIGHT_VISIBLE_SURFACE : 0);
		L.shape_data[0]   = glm::vec4( l.half_extent, 1);
		L.shape_data[1]   = glm::vec4(-l.half_extent, 1);
		L.shape_data[2].x = l.thickness;

		const auto extent_dir = glm::normalize(l.half_extent);
		auto orientation = glm::rotation(glm::vec3(0, 0, 1), extent_dir);//ident_quat;
		// const auto cos_angle = glm::dot(glm::vec3(0, 0, 1), extent_dir);
		// if(cos_angle < 0.999f and cos_angle > -0.999f) // not parallel
		// 	orientation = glm::rotation(glm::vec3(0, 0, 1), extent_dir);
		L.shape_data[4] = glm::vec4(orientation.x, orientation.y, orientation.z, orientation.w);
		L.outer_angle = glm::length(l.half_extent)*2;  // size
	}
	else if constexpr (std::same_as<LT, SphereLight> or std::same_as<LT, SphereLightParams>)
	{
		L.type_flags      = LIGHT_TYPE_SPHERE \
			| (l.visible_surface? LIGHT_VISIBLE_SURFACE : 0);
		L.shape_data[0].x = l.radius;
	}
	else if constexpr (std::same_as<LT, DiscLight> or std::same_as<LT, DiscLightParams>)
	{
		L.type_flags      = LIGHT_TYPE_DISC | (l.double_sided? LIGHT_DOUBLE_SIDED: 0) \
			| (l.visible_surface? LIGHT_VISIBLE_SURFACE : 0);
		set_direction(L, l.direction);
		L.shape_data[0].x = l.radius;
	}

	L.type_flags     |= LIGHT_ENABLED;
	if(l.shadow_caster)
		L.type_flags |= LIGHT_SHADOW_CASTER;
	if(l.fog > 0)
		L.type_flags |= LIGHT_VOLUMETRIC;

	CLR_SHADOW_IDX(L);

	set_intensity(L, l.intensity);  // also sets affect_range

	// some verifications
	assert(L.position != glm::vec3(0));  // arguable...
	assert(L.color != glm::vec3(0));
	assert(L.intensity > 0);
	assert(IS_DIR_LIGHT(L) or L.affect_radius > 0);
	assert(L.fog_intensity <= 1 and L.fog_intensity >= 0);
	assert(not IS_SPOT_LIGHT(L) or L.spot_bounds_radius > 0);

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
	else if constexpr (std::same_as<LTP, RectLightParams>)
	{
		lt.position     = ltp.position;
		lt.orientation  = ltp.orientation;
		lt.size         = ltp.size;
		lt.double_sided = ltp.double_sided;
	}
	else if constexpr (std::same_as<LTP, TubeLightParams>)
	{
		lt.half_extent = ltp.half_extent;
		lt.thickness   = ltp.thickness;
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
	if constexpr (std::is_same_v<LT, RectLight>)
		return type_name(LIGHT_TYPE_RECT);
	if constexpr (std::is_same_v<LT, TubeLight>)
		return type_name(LIGHT_TYPE_TUBE);
	if constexpr (std::is_same_v<LT, SphereLight>)
		return type_name(LIGHT_TYPE_SPHERE);
	if constexpr (std::is_same_v<LT, DiscLight>)
		return type_name(LIGHT_TYPE_DISC);
	return std::string_view("{unknown}");
}

} // RGL
