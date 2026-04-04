#include "shadow_atlas.h"

#include <glm/vec3.hpp>

#include "light_constants.h"
#include "light_manager.h"
#include "light_wrapper.h"
#include "constants.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/epsilon.hpp"
#include "camera.h"
#include "scene.h"

#include <chrono>
#include <ranges>
#include <string_view>
#include <format>
#include <cstdio>
#include <cmath> // std::signbit

#include "buffer_binds.h"
#include "log.h"
#include "formatters_glm.h" // IWYU pragma: keep

namespace RGL
{

#define LIGHT_PVS_KEY(light_id, slot_idx) ((uint32_t(light_id) << 3) | (slot_idx))

using namespace std::literals;

static constexpr float s_min_light_value = 1e-2f;


#define SLOT_CONFIG(light_type)   ((light_type) == LightType::Point? SlotConfig::Cube: ((light_type) == LightType::Directional? SlotConfig::Cascades: SlotConfig::Single))


using namespace std::chrono;
using namespace std::literals;

inline glm::uvec4 mk_rect(const SpatialAllocator<uint32_t>::Rect &r, uint32_t margin=0)
{
	return glm::uvec4(r.x + margin, r.y + margin, r.w - 2*margin, r.h - 2*margin);
}

template<typename T>
constexpr T sign(T val)
{
	return (T(0) < val) - (val < T(0));   // (bool - bool) ... yeah
}

template<typename T>
constexpr T sign_cmp(T a, T b)
{
	return sign(a - b);
}

static const auto s_slot_max_size_shift = 3;

ShadowAtlas::ShadowAtlas(uint32_t size, LightManager &lights) :
	Texture2d(),
	_lights(lights),
	_min_change_interval(1s),
	_shadow_slots_info_ssbo("shadow-params"),
	_allocator(size, s_slot_max_size_shift + 3, s_slot_max_size_shift)
{
	assert(_allocator.size() >= 1024 and _allocator.size() <= 16384);

	_shadow_slots_info_ssbo.bindAt(SSBO_BIND_SHADOW_SLOTS_INFO);

	_id_to_allocated.reserve(64);
	_light_pvs.reserve(64);

	init_slots(24, 64, 256);
}

void ShadowAtlas::init_slots(size_t count0, size_t count1, size_t count2)
{
	clear();

	_distribution.clear();
	_distribution.reserve(_allocator.num_allocatable_levels());
	generate_slots({ count0, count1, count2 });  // +1 for sun light

	// define minimum render interval for each slot size
	//                    { skip frames, interval }
	// TODO: these sohould be configurable
	_render_intervals.push_back({ 0,   0ms });
	_render_intervals.push_back({ 1,  25ms });
	_render_intervals.push_back({ 2,  50ms });
	_render_intervals.push_back({ 4, 100ms });
}

ShadowAtlas::~ShadowAtlas()
{
	release();
}

bool ShadowAtlas::create()
{
	const auto size = _allocator.size();

	namespace C = RenderTarget::Color;
	namespace D = RenderTarget::Depth;
	// store 2-omponent normals as well as depth
	Texture2d::create("shadow-atlas", size, size, C::Texture | C::Float2, D::Texture | D::Float);
	// TODO: if we only use the color attachment (i.e. the normals) for slope comparison,
	//   we really only need a single-channel float (basically the cos(light_to_fragment_angle)).

	return bool(this);
}

size_t ShadowAtlas::update_allocations(const std::vector<LightIndex> &relevant_lights, const glm::vec3 &view_pos, const glm::vec3 &view_forward)
{
	const Time T0 = steady_clock::now();

	static std::vector<ValueLight> valued_lights;
	valued_lights.reserve(std::max(64ul, relevant_lights.size()));
	valued_lights.clear();

	static dense_set<LightID> seen_lights;
	seen_lights.reserve(valued_lights.capacity());
	seen_lights.clear();

	// 1. assign value to all, shadow-casting, lights
	evaluate_lights(relevant_lights, view_pos, view_forward, valued_lights, seen_lights);

	Counters counters;

	// "drop" all allocations for lights we didn't even see
	for(const auto &[light_id, _]: _id_to_allocated)
	{
		if(not seen_lights.contains(light_id))
		{
			if(remove_allocation(light_id))
				++counters.dropped;
		}
	}
	if(seen_lights.empty())
		return counters.dropped;

	// 2. "pour" the valued lights into the sizd -buckets.
	//    NOTE: this is just a "desire"; not affected by already allocated slots

	static std::vector<AtlasLight> desired_slots;
	desired_slots.reserve(std::max(64ul, valued_lights.size()));
	desired_slots.clear();

	counters += compute_desired(valued_lights, desired_slots);
	// _dump_desired(desired_slots);

	// 3. apply the desired slots; actually allocate the slots & assign to the AtlasLight entry
	counters += apply_desired_slots(desired_slots, T0);

	const auto num_changes = counters.changed();
	if(num_changes)
	{
		log_changes(counters, valued_lights.size(), T0);
#if defined(DEBUG)
		std::print(" ->");
		debug_dump_allocated(false);
#endif
	}

	// return how many shadow maps changed  (new, dropped, promoted, demoted)
	return num_changes;
}

void ShadowAtlas::log_changes(const Counters &counters, size_t num_prio, Time start_time)
{
	std::string msg;
	msg.reserve(32);
	std::format_to(std::back_inserter(msg), "\x1b[32;1mShadowAtlas\x1b[m {} lights ->", num_prio);
	if(counters.retained)
		std::format_to(std::back_inserter(msg), " \x1b[1m=\x1b[m{}", counters.retained);
	if(counters.allocated)
		std::format_to(std::back_inserter(msg), " \x1b[33;1m⭐\x1b[m{}", counters.allocated);
	if(counters.dropped)
		std::format_to(std::back_inserter(msg), " \x1b[31;1m❌\x1b[m{}", counters.dropped);
	if(counters.denied)
		std::format_to(std::back_inserter(msg), " \x1b[31;1m!\x1b[m{}", counters.denied);
	if(counters.promoted)
		std::format_to(std::back_inserter(msg), " \x1b[32;1m🡅\x1b[m{}", counters.promoted);
	if(counters.demoted)
		std::format_to(std::back_inserter(msg), " \x1b[34;1m🡇\x1b[m{}", counters.demoted);
	if(counters.change_pending)
		std::format_to(std::back_inserter(msg), " \x1b[1m❔\x1b[m{}", counters.change_pending);
	std::format_to(std::back_inserter(msg), ", in {}", duration_cast<microseconds>(steady_clock::now() - start_time));
	Log::info("atlas| {}", msg);
}

ShadowAtlas::SlotMask ShadowAtlas::need_render(const AtlasLight &atlas_light, Time now, size_t light_hash, const Scene &scene) const
{
	// NOTE: see comment in renderShadowMaps() regarding static/dynamic caching

	if(atlas_light.is_dirty() or light_hash != atlas_light.hash)
		return SlotMaskAll;

	// light has changed, check the view for each slot whether there are dynamic objects
	// render if either:
	// - there are dynamic objects in view
	// - enough frames skipped
	// - enough time has passed

	SlotMask stale_slots { 0 };

	for(uint_fast8_t slot_idx = 0; slot_idx < atlas_light.num_slots; ++slot_idx)
	{
		const auto &objects = pvs(scene, atlas_light.uuid, slot_idx);

		if(not objects.dynamic_entities.empty())
		{
			stale_slots |= 1u << slot_idx;
			continue;
		}

		const auto size_idx = slot_size_idx(atlas_light.slots[slot_idx].size);
		assert(size_idx < _render_intervals.size());
		const auto &[skip_frames, interval] = _render_intervals[size_idx];

		// frames skipped or time elapsed
		const auto overdue = (skip_frames == 0 or atlas_light._frames_skipped < skip_frames)
			or (now - atlas_light._last_rendered) >= interval;

		if(overdue)
			stale_slots |= 1u << slot_idx;
		else if(atlas_light._frames_skipped)
			--atlas_light._frames_skipped;
	}

	return stale_slots;
}

bool ShadowAtlas::remove_allocation(LightID light_id)
{
	auto found = _id_to_allocated.find(light_id);
	if(found == _id_to_allocated.end())
		return false;

	for(auto slot = 0u; slot < found->second.num_slots; ++slot)
		_light_pvs.erase(LIGHT_PVS_KEY(light_id, slot));

	const auto &atlas_light = found->second;

	free_all_slots(atlas_light);

	_id_to_allocated.erase(found);
	_lights.clear_shadow_index(light_id);

	if(light_id == _sun_id)
	{
		_sun_id = NO_LIGHT_ID;
		_csm_params.clear();
	}

	return true;
}

std::vector<std::pair<ShadowAtlas::SlotSize, size_t>> ShadowAtlas::allocated_counts() const
{
	static dense_map<SlotSize, size_t> size_counts_map;
	if(size_counts_map.empty())
		size_counts_map.reserve(_distribution.size());
	size_counts_map.clear();

	for(const auto &[light_id, atlas_light]: _id_to_allocated)
	{
		auto slot_size = atlas_light.slots[0].size;
		auto found = size_counts_map.find(slot_size);
		if(found == size_counts_map.end())
			size_counts_map[slot_size] = 1;
		else
			++found->second;
	}

	std::vector<std::pair<SlotSize, size_t>> size_counts;
	size_counts.reserve(size_counts_map.size());
	for(const auto &[size, count]: size_counts_map)
		size_counts.push_back({ size, count });

	std::ranges::sort(size_counts, [](const auto &A, const auto &B) {
		return A.first > B.first;
	});

	return size_counts;
}

void ShadowAtlas::debug_dump_allocated(bool details) const
{
	Log::debug("atlas| === Allocated slots ({}):", _id_to_allocated.size());

	small_vec<size_t, 6> size_counts;
	size_counts.resize(_distribution.size());

#if defined(_DEBUG)
	auto num_used = 0u;
#endif
	for(const auto &[light_id, atlas_light]: _id_to_allocated)
	{
#if defined(_DEBUG)
		num_used += atlas_light.num_slots;
#endif
		for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
		{
			const auto slot_size = atlas_light.slots[idx].size;
			const auto size_idx = _allocator.level_from_size(slot_size) - _allocator.level_from_size(_allocator.max_size());
			++size_counts[size_idx];
		}

		if(details)
		{
			const auto shadow_idx = _lights.shadow_index(light_id);
			if(shadow_idx != LIGHT_NO_SHADOW)
				Log::debug("atlas|   {{{}}}: {};  shadow idx: {}", light_id, sizes_count_summary(atlas_light), shadow_idx);
			else
				Log::debug("atlas|   {{{}}}: {};  shadow idx: --", light_id, sizes_count_summary(atlas_light));
		}
	}
	if(not _id_to_allocated.empty())
	{
		Log::debug("atlas|   Totals {{ {} }}", sizes_count_summary(size_counts));
#if defined(_DEBUG)
		auto num_available = 0u;
		for(const auto &slot_set: _available_slots)
			num_available += slot_set.size();

		assert(num_available + num_used == _total_num_slots);
#endif
	}
}

void ShadowAtlas::debug_dump_available()
{
	small_vec<size_t, 6> available_counts;
	available_counts.resize(_distribution.size());

	auto size_idx = 0u;
	for(const auto &slot_set: _available_slots)
	{
		available_counts[size_idx] += slot_set.size();
		++size_idx;
	}

	Log::debug("atlas| Available: {{ {} }}", sizes_count_summary(available_counts));
}

void ShadowAtlas::debug_dump_desired(const std::vector<AtlasLight> &desired_slots) const
{
	Log::debug("atlas| === Desired slots ({}):", desired_slots.size());
	for(const auto &atlas_light: desired_slots)
		Log::debug("atlas|   {{{}}}: {}", atlas_light.uuid, sizes_count_summary(atlas_light));
}

void ShadowAtlas::update_slots_ssbo()
{
	// NOTE: if a "sun" light has shadow map allocated (see allocated_sun()), update_csm_params() needs to be called before this

	static std::vector<decltype(_shadow_slots_info_ssbo)::value_type> slots_params;

	slots_params.reserve(_id_to_allocated.size());
	slots_params.clear();

	for(auto &[light_id, atlas_light]: allocated_lights())
	{
		const auto light_ = _lights.get_light(light_id);
		if(not light_) // e.g. not enabled
		{
			// Log::debug("atlas| {{{}}} no such light", light_id);
			continue;
		}
		const auto &light = *light_;

		std::array<glm::mat4, MAX_SLOTS>  view_projs;
		std::array<glm::uvec4, MAX_SLOTS> rects;
		std::array<float, MAX_SLOTS>      texel_sizes;

		for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
		{
			rects[idx] = atlas_light.slots[idx].rect;

			if(light.general.light_type == LightType::Directional)
			{
				view_projs[idx] = _csm_params.light_view_projection[idx];
				texel_sizes[idx] = (_csm_params.view_aabb[idx].min().z - _csm_params.view_aabb[idx].max().z) / float(rects[idx].z);
				// Log::debug("atlas|  csm[{}] view proj: {:.4f}", idx, view_projs[idx]);
			}
			else
			{
				const auto &[view, proj, nz, fz] = light_view_projection(light, idx);
				view_projs[idx] = proj * view;
				texel_sizes[idx] = (fz - nz) / float(rects[idx].z);
			}
		}

		const auto shadow_idx = slots_params.size();  // i.e. the entry we're about to add
		slots_params.emplace_back(view_projs, rects, texel_sizes);

		_lights.set_shadow_index(light_id, shadow_idx);
	}
	_shadow_slots_info_ssbo.set(slots_params);
	_lights.flush();
}

static constexpr glm::vec3 s_cube_face_forward[] = {
	 AXIS_X,
	-AXIS_X,
	 AXIS_Y,
	-AXIS_Y,
	 AXIS_Z,
	-AXIS_Z,
};
static constexpr glm::vec3 s_cube_face_up[] = {
	-AXIS_Y,
	-AXIS_Y,
	 AXIS_Z,
	-AXIS_Z,
	-AXIS_Y,
	-AXIS_Y,
};

ShadowAtlas::LightViewProjection ShadowAtlas::light_view_projection(const LightWrapper &light, size_t idx) const
{
	// TODO: this is actually only needed if the light has changed. cache it where?

	const auto far_z  = light.gpu_light.affect_radius;
	const auto near_z = std::max(0.1f, far_z / 500.f);

	const auto &position = light.transform.position();

	if(light.general.light_type == LightType::Point)
	{
		assert(idx < 6);
		static constexpr auto square = 1.f;

		const auto &view_forward   = s_cube_face_forward[idx];
		const auto &view_up        = s_cube_face_up[idx];

		assert(glm::epsilonEqual(glm::length(view_forward), 1.f, 0.01f));
		const auto light_view      = glm::lookAt(position, position + view_forward, view_up);
		const auto face_projection = glm::perspective(glm::half_pi<float>(), square, near_z, far_z);

		return { light_view, face_projection, near_z, far_z };
	}
	if(light.general.light_type == LightType::Spot)
	{
		assert(idx == 0);
		static constexpr auto square = 1.f;

		const auto &view_forward = light.gpu_light.direction;
		// if 'view_forward' is close to Y axis, use X axis as "up"
		const auto &view_up      = glm::all(glm::lessThan(glm::abs(view_forward) - AXIS_Y, glm::vec3(1e-3f, 1e-3f, 1e-3f))) ? AXIS_X: AXIS_Y;

		const auto light_view       = glm::lookAt(position, position + view_forward, view_up);
		const auto light_projection = glm::perspective(light.gpu_light.outer_angle*2, square, near_z, far_z);

		return { light_view, light_projection, near_z, far_z };
	}
	else
	{
		assert(false);
		// for directional lights, the necessary stuff is calculated in update_csm_params()
	}

	return { glm::mat4(1), glm::mat4(1), -1, -1 };
}

const QueryResult &ShadowAtlas::pvs(const Scene &scene, LightID light_id, uint_fast8_t slot_idx) const
{
	QueryResult *result { nullptr };
	auto found = _light_pvs.find(LIGHT_PVS_KEY(light_id, slot_idx));
	if(found != _light_pvs.end())
		result = &found->second;
	else
	{
		QueryResult res;
		// insert & get address of inserted object
		// TODO: possibly to do in a cleaner way?
		result = &((*_light_pvs.insert({ LIGHT_PVS_KEY(light_id, slot_idx), res }).first).second);
	}

	const auto light_ = _lights.get_light(light_id);
	if(not light_) // e.g. not enabled
	{
		// any previous result is kept
		return *result;
	}

	const auto &light = *light_;

	switch(light.general.light_type)
	{
	case LightType::Directional:
	{
		// TODO: use the light-space AABB
		const auto &view       = _csm_params.light_view[slot_idx];
		const auto &projection = _csm_params.light_projection[slot_idx];
		const auto &aabb       = _csm_params.view_aabb[slot_idx];
		scene.query(view, projection, aabb, *result);
	}
	break;

	case LightType::Point:
	{
		const auto &[view, proj, near, far] = light_view_projection(light, slot_idx);
		Frustum frustum;
		frustum.setFromView(proj, view, light.transform.position());
		scene.query(frustum, *result);
	}
	break;

	case LightType::Spot:
	{
		const auto &[view, proj, near, far] = light_view_projection(light);
		Frustum frustum;
		frustum.setFromView(proj, view, light.transform.position());
		scene.query(frustum, *result);
	}
	break;

	default:
		assert(false);
	}

	return *result;
}

	   // frustum corners in NFC space (always the same)
static constexpr std::array<glm::vec4, 8> s_frustum_corners_ndc = {
	glm::vec4(-1, -1, -1,  1),
	glm::vec4(-1, -1,  1,  1),
	glm::vec4(-1,  1, -1,  1),
	glm::vec4(-1,  1,  1,  1),
	glm::vec4( 1, -1, -1,  1),
	glm::vec4( 1, -1,  1,  1),
	glm::vec4( 1,  1, -1,  1),
	glm::vec4( 1,  1,  1,  1),
};

const ShadowAtlas::CSMParams &ShadowAtlas::update_csm_params(LightID light_id, const Camera &camera)//, float radius_uv)
{
	const auto light_maybe = _lights.get_light(light_id);
	if(not light_maybe)
	{
		Log::error("atlas| {{{}}} no such light", light_id);
		_csm_params.clear();
		return _csm_params;
	}

	const auto &sun = *light_maybe;

	if(not sun.general.enabled)
	{
		_csm_params.clear();
		return _csm_params;
	}

	auto found = _id_to_allocated.find(light_id);
	if(found == _id_to_allocated.end())
	{
		assert(false);
		return _csm_params;
	}
	const auto &atlas_light = found->second;

	const auto num_cascades = atlas_light.num_slots;
	assert(num_cascades >= 1 and num_cascades <= 6);
	assert(num_cascades == _sun_num_cascades);


	_csm_params.num_cascades = num_cascades;

	// this bit needs only be done when the frustum's near/far planes has changed
	float near_z  = camera.nearPlane();
	float far_z   = std::min(camera.farPlane(), _max_distance);//camera.farPlane() * range_scale;   // distance is limited below  (using _max_distance)
	float z_range = far_z - near_z;
	float z_ratio = far_z / near_z;

	const auto inv_view_proj = glm::inverse(camera.projectionTransform(near_z, far_z) * camera.viewTransform());
	std::array<glm::vec3, 8> frustum_corners_ws;
	for(auto idx = 0u; idx < 8; ++idx)
	{
		auto corner_ws = inv_view_proj * s_frustum_corners_ndc[idx];
		corner_ws /= corner_ws.w;
		frustum_corners_ws[idx] = corner_ws;
	}

	// Log::debug("atlas|  N: {:.2f}-{:.1f}  range: {:.2f}  ratio: {:.3f}", near_z, far_z, z_range, z_ratio);

	float previous_split_far  = camera.nearPlane();
	float max_frustum_size { 0.f };

	// Log::debug("atlas| cam   pos: {:.2f}; {:.2f}; {:.2f}", camera.position().x, camera.position().y, camera.position().z);
	// Log::debug("atlas| light dir: {:.5f}; {:.5f}; {:.5f}", sun.direction.x, sun.direction.y, sun.direction.z);

#define STABLE_PROJECTION

	for(auto cascade = 0u; cascade < num_cascades; ++cascade)
	{
		// split frustum in depth slices; blend between logarithmic and linear
		float p        = float(cascade + 1) / float(num_cascades);
		float d_log    = near_z * std::pow(z_ratio, p);
		float d_linear = near_z + z_range * p;
		float d_mix    = _csm_frustum_split_mix*(d_log - d_linear) + d_linear;//glm::mix(d_linear, d_log, _csm_frustum_split_mix) + d_linear;


		// const auto split_near = previous_split_far;
		const float split_far = camera.nearPlane() + (d_mix - camera.nearPlane());//*range_scale;
		const float far_frac = (split_far - near_z) / z_range;
		const float near_frac = (previous_split_far - near_z) / z_range;

		// Log::debug("atlas|  [{}] lin: {:.4f}  log: {:.4f} -> {:.5f} frac.: {:.5f}", cascade, d_linear, d_log, split_far, far_frac);
		_csm_params.split_depth[cascade] = -split_far; // store depth to the far side of the split (to deduce cascade index in the shader)

		previous_split_far = split_far;

		//   also the center point as we go
		auto cascade_center_ws = glm::vec3(0);

		std::array<glm::vec3, 8> cascade_corners_ws;
		// construct the ws corners by creating a new projection matrix per cascade, using the split_near/far
		// const auto cascade_projection = camera.projectionTransform(split_near, split_far);
		// const auto inv_view_proj = glm::inverse(cascade_projection * camera.viewTransform());
		// for(auto idx = 0u; idx < 8; ++idx)
		// {
		// 	auto corner_ws = inv_view_proj * s_frustum_corners_ndc[idx];
		// 	corner_ws /= corner_ws.w;
		// 	cascade_corners_ws[idx] = corner_ws;
		// 	cascade_center_ws += glm::vec3(corner_ws);
		// }
		// cascade_center_ws /= 8.f;

		auto slice_frustum = [&frustum_corners_ws, &cascade_corners_ws, far_frac, near_frac, &cascade_center_ws](auto idx) {
			const glm::vec3 dist = frustum_corners_ws[idx + 1] - frustum_corners_ws[idx]; // far - near
			cascade_corners_ws[idx]     = frustum_corners_ws[idx] + (dist * near_frac);
			cascade_corners_ws[idx + 1] = frustum_corners_ws[idx] + (dist * far_frac);

			cascade_center_ws += cascade_corners_ws[idx];
			cascade_center_ws += cascade_corners_ws[idx + 1];
		};
		slice_frustum(0u);
		slice_frustum(2u);
		slice_frustum(4u);
		slice_frustum(6u);
		cascade_center_ws /= 8.f;

		// Log::debug("atlas|   light 'pos': {:.5f}; {:.5f}; {:.5f}", light_pos.x, light_pos.y, light_pos.z);
		// Log::debug("atlas|   WS center: {:.5f}; {:.5f}; {:.5f}", cascade_center_ws.x, cascade_center_ws.y, cascade_center_ws.z);

		// build AABB in light-space
		bounds::AABB cascade_aabb_ls;
		float radius_sq = 0.f;
		for(const auto &corner_ws: cascade_corners_ws)
		{
			const auto to_corner_ws = corner_ws - cascade_center_ws;
			radius_sq = std::max(radius_sq, glm::dot(to_corner_ws, to_corner_ws));
		}

		[[maybe_unused]] auto cascade_radius = std::sqrt(radius_sq);
		// quantize slightly, probably for some good reason :)
		cascade_radius = std::ceil(cascade_radius * 16.0f) / 16.0f;

		const auto z_offset = _csm_cascade_backoff;

		const auto max_extents = glm::vec3(cascade_radius);
		const auto min_extents = -max_extents;

		auto minZ = min_extents.z;
		auto maxZ = max_extents.z;
		// extend Z towrads the camera (might cast shadows into the camera frustum (e.g. a tall ceiling)
		minZ *= minZ < 0? z_offset: 1/z_offset;

		const auto light_projection_distance = minZ;

		const float ortho_depth = maxZ - minZ;

		const auto light_pos = cascade_center_ws - sun.gpu_light.direction * -light_projection_distance;
		auto light_view = glm::lookAt(light_pos, cascade_center_ws, AXIS_Y);
		auto light_projection = glm::ortho(min_extents.x, max_extents.x, min_extents.y, max_extents.y, 0.f, ortho_depth);
		_csm_params.view_aabb[cascade] = bounds::AABB({ min_extents.x, min_extents.y, 0.f }, { max_extents.x, max_extents.y, ortho_depth });

		max_frustum_size = glm::max(max_frustum_size, max_extents.x - minZ);

		_csm_params.near_far_plane[cascade] = { minZ, maxZ };  // store depth to the far side of the split


		auto light_vp = light_projection * light_view;

		if(_csm_stabilization)
		{
			// apply "stabilization" logic; to reduce pixel "swimming" when camera moves
			const auto shadow_size = float(atlas_light.slots[cascade].size);
			auto shadow_origin = light_vp * glm::vec4(0, 0, 0, 1);
			shadow_origin *= shadow_size / 2.f;

			auto rounded_origin = glm::round(shadow_origin);
			auto round_offset = rounded_origin - shadow_origin;
			round_offset *= 2.f / shadow_size;
			round_offset.z = 0.f;
			round_offset.w = 0.f;

			light_projection[3] += round_offset; // inject into projection matrix
			light_vp = light_projection * light_view;
		}

		_csm_params.light_view[cascade]            = light_view;
		_csm_params.light_projection[cascade]      = light_projection;
		_csm_params.light_view_projection[cascade] = light_vp;

#if 0
		Log::debug("atlas| cascade {}  F:{: >7.3f}  C:{: >8.4f}  +{:6.1f} (D:{:5.1f}) -> L:{: >8.4f}  R:{: >7.3f}",
				   cascade,
				   split_far,
				   light_pos,
				   -light_projection_distance,
				   ortho_depth,
				   cascade_center_ws,
				   cascade_radius
				   );

		Log::debug("atlas|   [{}] view-proj:\n{: 8.4f}", cascade, light_vp);
#endif
		// _csm_params.depth_range[cascade] = { minZ, maxZ };

		// std::exit(42);
	}

	_csm_params.light_radius_uv = _csm_light_radius_uv / max_frustum_size;

	return _csm_params;
}

const ShadowAtlas::AtlasLight & ShadowAtlas::allocated_sun() const
{
	if(_sun_id == NO_LIGHT_ID)
	{
		static const AtlasLight sentinel;
		return sentinel;
	}

	assert(_id_to_allocated.contains(_sun_id));
	assert(_lights.contains(_sun_id));

	return _id_to_allocated.find(_sun_id)->second;
}

void ShadowAtlas::clear()
{
	// Counters counters;
	for(const auto &[light_id, atlas_light]: _id_to_allocated)
	{
		if(remove_allocation(light_id))
			;//++counters.dropped;
	}
	_id_to_allocated.clear();

//	_dump_changes(counters);
}

void ShadowAtlas::evaluate_lights(const std::vector<LightIndex> &relevant_lights, const glm::vec3 &view_pos, const glm::vec3 &view_forward, std::vector<ShadowAtlas::ValueLight> &valued_lights, dense_set<LightID> &seen_lights)
{
	// calculate a "value" for each shadow-casting light

	float strongest_dir_value { s_min_light_value };  // only the strongest dir light may get a shadow allocation
	LightID strongest_dir_id { NO_LIGHT_ID };

	for(const auto &light_index: relevant_lights)
	{
		const auto light_id = _lights.light_id(light_index);

		const auto light_ent = entt::entity(light_id);
		const auto &[general, transform] = _lights.entities().get<component::LightGeneral, component::Transform>(light_ent);

		seen_lights.insert(light_id);

		if(general.light_type == LightType::Directional and general.intensity > strongest_dir_value)
		{
			strongest_dir_id = light_id;
			strongest_dir_value = general.intensity;
		}
		else
		{
			const auto affect_radius = _lights.affect_radius(light_id, general);
			const bounds::Sphere light_sphere { transform.position(), affect_radius };
			const auto value = evaluate_light(light_sphere, view_pos, view_forward);

			if(value > s_min_light_value)
				valued_lights.emplace_back(value, light_id, SLOT_CONFIG(general.light_type));
		}
	}

	if(strongest_dir_value > s_min_light_value)
	{
		// the "sun" should _always_ get a shadow slot
		valued_lights.emplace_back(2.f, strongest_dir_id, SlotConfig::Cascades);
	}

	// highest-valued light first
	std::ranges::sort(valued_lights, std::greater<>{});
}

float ShadowAtlas::evaluate_light(const bounds::Sphere &light_sphere, const glm::vec3 &view_pos, const glm::vec3 &view_forward) const
{
	// calculate the "value" of a light on a fixed scale  [0, 1]

	assert(_max_distance > 0);

	const auto edge_distance = std::max(0.f, glm::distance(light_sphere.center(), view_pos) - light_sphere.radius());
	if(edge_distance >= _max_distance) // too far away
		return 0.f;

	const auto normalized_dist = edge_distance / _max_distance;
	// nrmalize the radius using a "large" radius
	const auto normalized_radius = std::min(light_sphere.radius() / _large_light_radius, 1.f);

	const auto importance = std::min(1.2f * normalized_radius / glm::max(normalized_dist, 1e-4f), 1.f);
	const auto base_weight = importance * importance; // inverse square falloff

	const auto type_weight = 1.f; // e.g. 0.8f for point, 1.f for spot, etc.
	float facing_weight = 1.f;
	if(edge_distance > 0)  // i.e. outside  light affect radius
	{
		// outside the light's radius decrease based on facing angle

		static const auto cutoff = std::cos(glm::radians(45.f)); // start decrease at 45 degrees
		static const auto min_dot = 0.f;

		const auto facing = glm::dot(glm::normalize(light_sphere.center()- view_pos), view_forward);
		if(facing < cutoff)
		{
			facing_weight = glm::clamp((facing - min_dot) / (cutoff - min_dot), 0.f, 1.f);
			facing_weight = 0.5f + 0.5f * facing_weight; // scales from 0.5 (behind) to 1.f (in front)
		}
	}
	else
	{
	  // TODO: player's shadow might be visible
	  //   essentially the inverse of the above, booost if facing away from the light
	}

	const auto manual_priority = 1.f;  // TODO light.priority  [0, 1] range, default = 1
	const auto dynamic_boost = 1.f;    // TODO light.has_dynamic_content ? 1.f : 0.9f;

	const auto value = glm::clamp(base_weight * type_weight * facing_weight * manual_priority * dynamic_boost, 0.f, 1.f);

		   // std::print("  D {:.0f}%  importance {:4.2f}   base {:4.2f}  facing {:4.2f}  ->  {:4.2f}\n",
		   // 		   100.f*distance/_max_distance, importance, base_weight, facing_weight, value);

	return value;
}

ShadowAtlas::Counters ShadowAtlas::compute_desired(const std::vector<ValueLight> &valued_lights, std::vector<AtlasLight> &desired_slots)
{
	Counters counters;
	// make a local copy of the distribution, so we can modify it
	auto distribution = _distribution;

	// Log::debug("atlas|  start_size_idx: {}   top value: {}  [{}]", size_idx, top_light_value, prioritized.begin()->light_id);

	auto valued_iter = valued_lights.begin();

	// if the first light is a "sun", allocate it separately (different logic, i.e. CSM)
	//   and there's no need to check for available space since it's the first light
	if(auto &valued_light = *valued_iter; valued_light.config == SlotConfig::Cascades)
	{
		++valued_iter;

		AtlasLight atlas_light;
		atlas_light.uuid        = valued_light.light_id;
		atlas_light.num_slots   = _sun_num_cascades;
		atlas_light.slot_config = SlotConfig::Cascades;

			   // allocate cascaded shadow map for the sun (N largest slot sizes)
		const auto slot_size = _allocator.max_size();
		for(auto cascade = 0u; cascade < _sun_num_cascades; ++cascade)
			atlas_light.slots[cascade].size = slot_size;
		distribution[slot_size_idx(slot_size)] -= _sun_num_cascades;

		desired_slots.push_back(atlas_light);
	}

		   // handle all "regular" lights
	for(; valued_iter != valued_lights.end(); ++valued_iter)
	{
		const auto &prio_light = *valued_iter;

		AtlasLight atlas_light;
		atlas_light.uuid        = prio_light.light_id;
		atlas_light.slot_config = prio_light.config;

		if(auto cfg = prio_light.config; cfg != SlotConfig::Cascades)
			atlas_light.num_slots = uint_fast8_t(cfg);
		else
			assert(false); // should never happen; there can be only one! (see above)

			   // based on the light's value, deduce where to start searching for available slots
		auto size_idx = static_cast<uint32_t>(std::floor(static_cast<float>(_distribution.size()) * (1 - std::min(prio_light.value, 1.f))));
		// initial slot size for the light's value
		auto slot_size = _allocator.max_size() >> size_idx;

		// find a slot size tier that still has room for required slots
		while(distribution[size_idx] < atlas_light.num_slots and size_idx < _distribution.size())
		{
			// not enough available here, next size
			++size_idx;
			slot_size >>= 1;
		}

		const auto slot_found = size_idx < _distribution.size(); // above loop exited early, i.e. found available slots
		if(slot_found)
		{
			// define the desired slot(s)
			for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
				atlas_light.slots[idx].size = slot_size;

			desired_slots.push_back(atlas_light);
			distribution[size_idx] -= atlas_light.num_slots;
			// Log::debug("atlas|  [{:>2}] desired {} {:>4} slots  -> {:>3} remaining",
			// 		   atlas_light.uuid, atlas_light.num_slots, slot_size, distribution[size_idx]);
		}
		else
		{
			// no slots available, remove any previous allocation
			// TODO: ideally, this should not be done here (should be in update_allocations())
			Log::warning("atlas| [{}] can't fit {} slots", atlas_light.uuid, atlas_light.num_slots);
			if(remove_allocation(prio_light.light_id))
				++counters.dropped;
			else
				++counters.denied;
			if(atlas_light.num_slots == 1)
				break;  // not even a sincle-slot could be alllocated
		}
	}

	return counters;
}

ShadowAtlas::Counters ShadowAtlas::apply_desired_slots(const std::vector<AtlasLight> &desired_slots, const Time now)
{
	// std::puts("-- apply_desired_slots()");

	// small_vec<decltype(_shadow_params_ssbo)::value_type, 120> shadow_params;

	Counters counters;

	// size changes must be done in two phases; remember which they were
	small_vec<uint32_t, 120> changed_size;

	small_vec<size_t, 6> size_promised;  // max to min level slot sizes promised (used for pro/demotions)
	size_promised.resize(_distribution.size());

	// deallocate for pro/demotions first, then allocations for pro/demotions, then finally new allocations

	// deallocate lights for pro/demotions
	for(const auto &[desired_index, desired]: std::views::enumerate(desired_slots))
	{
		const auto light_id = desired.uuid;

		auto found = _id_to_allocated.find(light_id);
		if(found != _id_to_allocated.end())
		{
			// existing allocation, check if it desires to be changed

			auto &atlas_light = found->second;

			const auto size_diff = int32_t(desired.slots[0].size) - int32_t(atlas_light.slots[0].size);
			const auto change_age = now - atlas_light._last_size_change;

			if(size_diff == 0 or change_age < _min_change_interval or not has_slots_available(desired, size_promised))
			{
				++counters.retained;

				if(size_diff != 0)
					++counters.change_pending;
			}
			else
			{
				changed_size.push_back(uint32_t(desired_index));

				if(size_diff > 0)
					++counters.promoted;
				else
					++counters.demoted;

				// first deallocate the old size (new size allocated in the loop below)

				// return the previous size slot to the pool
				// std::print("  [{}]  free {} slots:    {}: {} (-> {})",
				// 		   light_id, atlas_light.num_slots, size_diff > 0?"pro":"dem", atlas_light.slots[0].size, desired.slots[0].size);
				// std::fflush(stdout);
				auto idx = atlas_light.num_slots;  // loop in reverse to put the slots back in the same order as allocated
				while(idx-- != 0)
				{
					const auto &slot = atlas_light.slots[idx];
					free_slot(slot.size, slot.node_index);

					// and we promise to allocate the new size later (loop below)
					++size_promised[slot_size_idx(desired.slots[idx].size)];
				}

				// TODO: possible to blit-copy the existing rendered slots to the new ones for demotions
				//   However, this should only be implemented if really beneficial on a larger scale!
				//   b/c implies a fair degree of compexity

				// std::print("; {} remaining\n", _slot_sets[atlas_light.slots[0].size].size());
			}
		}
	}

	// allocate the new slots for pro/demotions
	for(const auto index: changed_size)
	{
		const auto &desired = desired_slots[index];
		const auto light_id = desired.uuid;

		auto found = _id_to_allocated.find(light_id);
		assert(found != _id_to_allocated.end());

		auto &atlas_light = found->second;

		// std::print("  [{}] alloc {} slots:  pro/de -> {}", light_id, atlas_light.num_slots, desired.slots[0].size);
		// std::fflush(stdout);
		for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
		{
			auto &slot = atlas_light.slots[idx];

			// get the new slots from the desired slot size
			auto node_index = alloc_slot(desired.slots[idx].size);
			slot.node_index = node_index;
			slot.size = desired.slots[idx].size;
			slot.rect = mk_rect(_allocator.rect(node_index), 1);
		}

		atlas_light._last_size_change = now;
		atlas_light._dirty = true;

		found->second = atlas_light;
	}

	// no more promises!  (we already delivered on those above)
	size_promised.clear();
	size_promised.resize(_distribution.size());

	// finally, allocate totally NEW slots
	for(const auto &desired: desired_slots)
	{
		const auto light_id = desired.uuid;

		auto found = _id_to_allocated.find(light_id);
		if(found == _id_to_allocated.end())
		{
			// new shadow map allocation

			if(not has_slots_available(desired, size_promised))
			{
				// this should not happen, I think...

				if(remove_allocation(light_id))
					++counters.dropped;
				Log::error("atlas|   [{}] OUT OF SLOTS size {}", light_id, desired.slots[0].size);
				debug_dump_allocated(true);
				debug_dump_available();
				Log::debug("atlas| size_promised: {{ {} }}", sizes_count_summary(size_promised));
				debug_dump_desired(desired_slots);
				assert(false);
				continue;
			}

			++counters.allocated;

			auto atlas_light = desired;  // must copy :(   (surely not everything?)

			// std::print("  [{}] alloc {} slots:   new {}", light_id, atlas_light.num_slots, atlas_light.slots[0].size);
			// std::fflush(stdout);
			for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
			{
				const auto slot_size = atlas_light.slots[idx].size;

				const auto node_index = alloc_slot(slot_size);

				atlas_light.slots[idx].node_index = node_index;
				atlas_light.slots[idx].rect = mk_rect(_allocator.rect(node_index), 1);
			}

			Log::debug("atlas| {{{}}} allocated -> {}", light_id, sizes_count_summary(atlas_light));

			_id_to_allocated[light_id] = atlas_light;

			if(desired.slot_config == SlotConfig::Cascades)
				_sun_id = light_id;
		}
	}

	return counters;
}

std::string ShadowAtlas::sizes_count_summary(const AtlasLight &atlas_light) const
{
	small_vec<size_t, 6> size_counts;
	size_counts.resize(_distribution.size());
	for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
	{
		const auto slot_size = atlas_light.slots[idx].size;
		++size_counts[slot_size_idx(slot_size)];
	}

	return sizes_count_summary(size_counts);
}

std::string ShadowAtlas::sizes_count_summary(const small_vec<size_t, 6> &size_counts) const
{
	std::string str;
	str.reserve(32);
	for(auto size_idx = 0u; size_idx < size_counts.size(); ++size_idx)
	{
		const auto slot_size = _allocator.max_size() >> size_idx;
		const auto count = size_counts[size_idx];
		if(count)
		{
			if(not str.empty())
				str.append(", ");
			std::format_to(std::back_inserter(str), "{}x{}", count, slot_size);
		}
	}
	if(str.empty())
		str.append("-none-");
	return str;
}

bool ShadowAtlas::has_slots_available(const AtlasLight &atlas_light, const small_vec<size_t, 6> &size_promised) const
{
	struct SizeCount
	{
		SlotSize size;
		size_t count;
	};

	small_vec<SizeCount, 3> size_counts;   // at most 3 unique sizes

	for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
	{
		const auto &slot = atlas_light.slots[idx];

		bool found = false;
		for(auto &ss: size_counts)
		{
			found = ss.size == slot.size;
			if(found)
			{
				++ss.count;
				break;
			}
		}
		if(not found)
		{
			assert(size_counts.size() < 3);
			size_counts.push_back({ slot.size, 1 });
		}
	}

	for(const auto &ss: size_counts)
	{
		const auto size_idx = slot_size_idx(ss.size);
		const auto promised = size_promised[size_idx];
		const auto num_free = _available_slots[size_idx].size();
		if(num_free - promised < ss.count)
			return false;
	}

	return true;

	// auto need1_size = 0u;
	// auto need1 = 0u;
	// auto need2_size = 0u;
	// auto need2 = 0u;
	// auto need3_size = 0u;
	// auto need3 = 0u;

	// for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
	// {
	// 	auto slot_size = atlas_light.slots[idx].size;

	// 	if(need1_size == 0)
	// 	{
	// 		need1_size = slot_size;
	// 		++need1;
	// 	}
	// 	else if(need1_size == slot_size)
	// 		++need1;
	// 	else if(need2_size == 0)
	// 	{
	// 		need2_size = slot_size;
	// 		++need2;
	// 	}
	// 	else if(need2_size == slot_size)
	// 		++need2;
	// 	else if(need3_size == 0)
	// 	{
	// 		need3_size = slot_size;
	// 		++need3;
	// 	}
	// 	else if(need3_size == slot_size)
	// 		++need3;
	// }

	// if(_slot_sets.find(need1_size)->second.size() < need1)
	// 	return false;

	// if(need2_size == 0)
	// 	return true;
	// if(_slot_sets.find(need2_size)->second.size() < need2)
	// 	return false;

	// if(need3_size == 0)
	// 	return true;
	// if(_slot_sets.find(need3_size)->second.size() < need3)
	// 	return false;

	// return true;
}

ShadowAtlas::SlotID ShadowAtlas::alloc_slot(SlotSize slot_size, bool first)
{
	const auto size_idx = slot_size_idx(slot_size);

	assert(size_idx < _available_slots.size());

	auto &free_slots = _available_slots[size_idx];
	assert(not free_slots.empty());

	SlotID node_index;
	if(first)
	{
		node_index = free_slots.back();
		free_slots.pop_back();
	}
	else
	{
		// this is only used once; the reservatioon for a "sun" light
		node_index = free_slots.front();
		free_slots.erase(free_slots.begin());
	}

	// Log::debug("    alloc {:>4} -> {}    rem: {}", size, node_index, free_slots.size());

	return node_index;
}

void ShadowAtlas::free_slot(SlotSize slot_size, SlotID node_index)
{
	const auto size_idx = slot_size_idx(slot_size);

	assert(slot_size < _available_slots.size());

	auto &free_slots = _available_slots[size_idx];
	assert(free_slots.capacity() > free_slots.size()); // i.e. should never grow beyond its original size

	// Log::debug("     free {:>4} -- {}    rem: {}", size, node_index, free_slots.size());

#if defined(DEBUG)
	const auto &rect = _allocator.rect(node_index);
	Texture2d::clear({ rect.x, rect.y, rect.w, rect.h });
#endif

	free_slots.push_back(node_index);
}

void ShadowAtlas::free_all_slots(const AtlasLight &atlas_light)
{
	for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
	{
		const auto &slot = atlas_light.slots[idx];
		free_slot(slot.size, slot.node_index);
	}
}

void ShadowAtlas::generate_slots(std::initializer_list<size_t> distribution)
{
	const auto T0 = steady_clock::now();

	// size of 'distribution' should match number of allocatable levels - 1
	assert(distribution.size() == _allocator.num_allocatable_levels() - 1);
	_distribution.clear();

	auto idx = 0u;
	for(const auto count: distribution)
		_distribution.push_back(count);   // max_size -> min_size (last is calculated below)

	_total_num_slots = 0;

	// use the allocator to calculate how many slots are possible
	//  e.g. allocate the first N-1 levels of the distribution,
	//  than ask the allocator to count how many of the last size there is room left for
	auto slot_size = _allocator.max_size();
	auto size_idx = 0u;

	// chosen distribution + a "rest" level
	_available_slots.resize(_distribution.size() + 1);

	for(auto count: _distribution)
	{
		if(count == 0)
			break;

		auto &free_slots = _available_slots[size_idx];
		free_slots.clear();  // in case we're called more than once
		if(count > 0)
		{
			free_slots.reserve(count);

			for(auto idx = 0u; idx < count; ++idx)
			{
				const auto index = _allocator.allocate(slot_size);
				assert(index != _allocator.end());
				// prepend so they're allocated in "correct" order (uses pop_back())
				free_slots.insert(free_slots.begin(), index);
			}
			_total_num_slots += free_slots.size();
		}

		slot_size >>= 1;
		++size_idx;
	}

	// then the remaining space is allocated using the smallest size

	auto &free_slots = _available_slots[size_idx];
	// unfortunately we don't know how many there will be, but guessing plenty :)
	free_slots.reserve(256);
	do
	{
		const auto index = _allocator.allocate(slot_size);
		if(index != _allocator.end())
			free_slots.push_back(index);
		else
			break;
	}
	while(true);

	_total_num_slots += free_slots.size();

	// write number of smallest-sized slots
	_distribution.push_back(free_slots.size());

	const auto Td = steady_clock::now() - T0;

	Log::info("atlas| {} slots defined, in {}", _total_num_slots, duration_cast<microseconds>(Td));
	slot_size = _allocator.max_size();
	for(auto count: _distribution)
	{
		Log::info("atlas|  {:>4}: {} slots", slot_size, count);
		slot_size >>= 1;
	}
}


[[maybe_unused]] static constexpr std::string_view face_names[] = {
	"+X"sv, "-X"sv,
	"+Y"sv, "-Y"sv,
	"+Z"sv, "-Z"sv,
};

ShadowAtlas::AtlasLight::AtlasLight(const AtlasLight &other) :
	uuid(other.uuid),
	slot_config(other.slot_config),
	num_slots(other.num_slots),
	slots(other.slots),
	hash(0),
	_dirty(true),
	_frames_skipped(0)
{
}

} // RGL
