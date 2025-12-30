#include "shadow_atlas.h"

#include <glm/vec3.hpp>

#include "light_constants.h"
#include "light_manager.h"
#include "constants.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/epsilon.hpp"
#include "camera.h"

#include <chrono>
#include <ranges>
#include <string_view>
#include <format>
#include <cstdio>
#include <cmath> // std::signbit

#include "buffer_binds.h"
#include "log.h"

template <>
struct std::formatter<glm::mat4> {
	std::formatter<float> elem_fmt;
	bool pad_positive = false;

	constexpr auto parse(std::format_parse_context &ctx)
	{
		auto it = ctx.begin();
		if(it != ctx.end() and *it == ' ')
		{
			pad_positive = true;
			++it;
		}
		ctx.advance_to(it);
		return elem_fmt.parse(ctx);
	}

	auto format(const glm::mat4 &m, std::format_context &ctx) const
	{
		auto out = ctx.out();
		for(int row = 0; row < 4; ++row)
		{
			*out++ = '{';
			for(int col = 0; col < 4; ++col)
			{
				float val = m[row][col];
				if(pad_positive and not std::signbit(val))
					*out++ = ' ';
				out = elem_fmt.format(val, ctx);
				if(col < 3)
					*out++ = ';';
			}
			*out++ = '}';
			if(row < 3)
				*out++ = '\n';
		}
		return out;
	}
};

namespace RGL
{

using namespace std::literals;

static constexpr float s_min_light_value = 1e-2f;


#define SLOT_CONFIG(light)   (IS_POINT_LIGHT(light)? SlotConfig::Cube: (IS_DIR_LIGHT(light)? SlotConfig::Cascaded: SlotConfig::Single))


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

	init_slots(24, 64, 256);
}

void ShadowAtlas::init_slots(size_t count0, size_t count1, size_t count2)
{
	clear();

	_distribution.reserve(4);
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

size_t ShadowAtlas::eval_lights(const std::vector<LightIndex> &relevant_lights, const glm::vec3 &view_pos, const glm::vec3 &view_forward)
{
	const Time T0 = steady_clock::now();

	static std::vector<ValueLight> prioritized;
	prioritized.reserve(std::max(64ul, relevant_lights.size()));
	prioritized.clear();

	// 1. assign value to all, shadow-casting, lights
	auto counters = prioritize_lights(relevant_lights, view_pos, view_forward, prioritized);

	if(prioritized.empty())
	{
		size_t num_changes { 0 };
		for(const auto &[light_id, atlas_light]: _id_to_allocated)
		{
			bool deallocaded = remove_allocation(light_id);
			assert(deallocaded);
			++num_changes;
		}
		return num_changes;
	}

	// 2. "pour" the valued lights into the sizd -buckets.
	//    NOTE: this is just a "desire"; not affected by already allocated slots

	static std::vector<AtlasLight> desired_slots;
	desired_slots.reserve(std::max(64ul, prioritized.size()));
	desired_slots.clear();

	small_vec<size_t, 8> distribution(_distribution.begin(), _distribution.end());

	// Log::debug("  start_size_idx: {}   top value: {}  [{}]", size_idx, top_light_value, prioritized.begin()->light_id);

	auto prio_iter = prioritized.begin();

	// if the first light is a "sun", allocate it separately (different logic, i.e. CSM)
	//   and there's no need to check for available space since it's the first light
	if(auto &prio_light = *prio_iter; prio_light.config == SlotConfig::Cascaded)
	{
		++prio_iter;

		AtlasLight atlas_light;
		atlas_light.uuid        = prio_light.light_id;
		atlas_light.num_slots   = _sun_num_cascades;
		atlas_light.slot_config = SlotConfig::Cascaded;

		// allocate cascaded shadow map for the sun (N largest slot sizes)
		auto slot_size = _allocator.max_size();

		for(auto cascade = 0u; cascade < _sun_num_cascades; ++cascade)//, slot_size >>= 1)
		{
			atlas_light.slots[cascade].size = slot_size;
			--distribution[cascade];
		}

		desired_slots.push_back(atlas_light);
	}

	// handle all "regular" lights
	for(; prio_iter != prioritized.end(); ++prio_iter)
	{
		const auto &prio_light = *prio_iter;

		AtlasLight atlas_light;
		atlas_light.uuid        = prio_light.light_id;
		atlas_light.slot_config = prio_light.config;

		if(auto cfg = prio_light.config; cfg != SlotConfig::Cascaded)
			atlas_light.num_slots = uint_fast8_t(cfg);
		else
			assert(false); // should never happen; there can be only one! (see above)

		// based on the light's value, deduce where to start searching for available slots
		auto size_idx = static_cast<uint32_t>(std::floor(static_cast<float>(distribution.size()) * (1 - std::min(prio_light.value, 1.f))));
		// initial position and corresponding slot size
		auto slot_size = _allocator.max_size() >> size_idx;

		// find a slot size tier that still has room for required slots
		while(distribution[size_idx] < atlas_light.num_slots and size_idx < distribution.size())
		{
			// nothing available, next size
			++size_idx;
			slot_size >>= 1;
		}

		const auto slot_found = size_idx < distribution.size();

		if(slot_found)
		{
			// define the desired slot(s)
			for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
				atlas_light.slots[idx].size = slot_size;

			desired_slots.push_back(atlas_light);
			distribution[size_idx] -= atlas_light.num_slots;
			// Log::debug("  [{:>2}] desired {} {:>4} slots  -> {:>3} remaining",
			// 		   atlas_light.uuid, atlas_light.num_slots, slot_size, distribution[size_idx]);
		}
		else
		{
			// no slots available, remove any previous allocation
			Log::warning("[{}] can't fit {} slots", atlas_light.uuid, atlas_light.num_slots);
			if(remove_allocation(prio_light.light_id))
				++counters.dropped;
			else
				++counters.denied;
			if(atlas_light.num_slots == 1)
				break;  // not even a sincle-slot could be alllocated
		}
	}

	// _dump_desired(desired_slots);

	// 3. apply the desired slots; actually allocate the slots & assign to the AtlasLight entry
	counters += apply_desired_slots(desired_slots, T0);

	const auto num_changes = counters.changed();

	if(num_changes)
	{
		std::string msg;
		msg.reserve(32);
		std::format_to(std::back_inserter(msg), "\x1b[32;1mShadowAtlas\x1b[m {} lights ->", prioritized.size());
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
		std::format_to(std::back_inserter(msg), ", in {}", duration_cast<microseconds>(steady_clock::now() - T0));
		Log::info("{}", msg);
#if defined(DEBUG)
		std::print(" ->");
		debug_dump_allocated(false);
#endif
	}

	// return how many shadow maps changed  (new, dropped, promoted, demoted)
	return num_changes;
}

bool ShadowAtlas::should_render(const AtlasLight &atlas_light, Time now, size_t light_hash, bool has_dynamic) const
{
	if(atlas_light.is_dirty())
		return true;

	if(light_hash == atlas_light.hash and not has_dynamic)
		return false;


	// light has changed or there are dynamic objects within range!
	//   render if either enough frames skipped or enough time has passed  (AND ?)

	const auto size_idx = slot_size_idx(atlas_light.slots[0].size);
	assert(size_idx < _render_intervals.size());
	const auto &[skip_frames, interval] = _render_intervals[size_idx];

	const auto overdue = (skip_frames == 0 or atlas_light._frames_skipped < skip_frames)
		or (now - atlas_light._last_rendered) >= interval;

	if(not overdue and atlas_light._frames_skipped)
		--atlas_light._frames_skipped;

	return overdue;
}

bool ShadowAtlas::remove_allocation(LightID light_id)
{
	auto found = _id_to_allocated.find(light_id);
	if(found == _id_to_allocated.end())
		return false;

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
	static dense_map<SlotSize, size_t> size_counts;
	if(size_counts.empty())
		size_counts.reserve(_distribution.size());
	size_counts.clear();
	static std::vector<SlotSize> sizes;
	if(sizes.capacity() == 0)
		sizes.reserve(_distribution.size());
	sizes.clear();

	auto num_used = 0u;

	for(const auto &[light_id, atlas_light]: _id_to_allocated)
	{
		num_used += atlas_light.num_slots;

		for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
		{
			const auto slot_size = atlas_light.slots[idx].size;
			auto found = size_counts.find(slot_size);
			if(found == size_counts.end())
			{
				sizes.push_back(slot_size);
				size_counts[slot_size] = 1;
			}
			else
				++found->second;
		}

		if(details)
		{
			Log::debug("  - {:3}  {:2} slots; shadow idx: [{}]", light_id, atlas_light.num_slots, _lights.shadow_index(light_id));
			std::array<size_t, 4> alloc_counts = { 0, 0, 0, 0 };
			for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
				++alloc_counts[slot_size_idx(atlas_light.slots[idx].size)];
			std::string msg = "        sizes:";
			msg.reserve(32);
			for(const auto &[level, count]: std::views::enumerate(alloc_counts))
			{
				if(count)
					std::format_to(std::back_inserter(msg), " {:>4} {}", _allocator.max_size() >> level, count);
			}
			Log::debug("{}", msg);
		}
	}
	if(not sizes.empty())
	{
		std::ranges::sort(sizes, std::greater<SlotSize>());

		std::string msg = " {{ ";
		msg.reserve(32);
		auto first = true;
		for(const auto &slot_size: sizes)
		{
			if(not first)
				msg.append(", ");
			first = false;
			std::format_to(std::back_inserter(msg), "{}:{}", slot_size, size_counts[slot_size]);
		}
		msg.append(" }}");
		Log::debug("{}", msg);
#if defined(DEBUG)
		auto num_available = 0u;
		for(const auto &[size, slot_set]: _slot_sets)
			num_available += slot_set.size();

		assert(num_available + num_used == _total_num_slots);
#endif
	}
}

void ShadowAtlas::debug_dump_desired(const std::vector<AtlasLight> &desired_slots) const
{
	Log::debug("=== Desired slots ({}):", desired_slots.size());
	for(const auto &atlas_light: desired_slots)
	{
		Log::debug("  - {:3}  {:2} slots", atlas_light.uuid, atlas_light.num_slots);
		std::array<size_t, 4> alloc_counts = { 0, 0, 0, 0 };
		for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
			++alloc_counts[slot_size_idx(atlas_light.slots[idx].size)];
		std::string msg = "        sizes:";
		msg.reserve(32);
		for(const auto &[level, count]: std::views::enumerate(alloc_counts))
		{
			if(count)
				std::format_to(std::back_inserter(msg), " {:>4} {}", _allocator.max_size() >> level, count);
		}
		Log::debug("{}", msg);
	}
}

void ShadowAtlas::update_shadow_params()
{
	// NOTE: for directional lights, this uses the separately-updated CSM params (by calling update_csm_params())

	static std::vector<ShadowSlotInfo> shadow_params;
	static_assert(std::same_as<decltype(_shadow_slots_info_ssbo)::value_type, decltype(shadow_params)::value_type>);

	shadow_params.reserve(_id_to_allocated.size());
	shadow_params.clear();

	// if a "sun" light has shadow map allocated (see allocated_sun()), update_csm_params() needs to be called before this
	for(auto &[light_id, atlas_light]: allocated_lights())
	{
		const auto &light = _lights.get_by_id(light_id);

		std::array<glm::mat4, 6>  projs;
		std::array<glm::uvec4, 6> rects;
		std::array<float, 6>      texel_sizes;

		for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
		{
			rects[idx] = atlas_light.slots[idx].rect;

			if(IS_DIR_LIGHT(light))
			{
				projs[idx] = _csm_params.view_projection[idx];
				texel_sizes[idx] = (_csm_params.depth_range[idx].y - _csm_params.depth_range[idx].x) / float(rects[idx].z);
			}
			else
			{
				const auto &[proj, nz, fz] = light_view_projection(light, idx);
				projs[idx] = proj;
				texel_sizes[idx] = (fz - nz) / float(rects[idx].z);
			}
		}

		_lights.set_shadow_index(light_id, shadow_params.size());

		shadow_params.emplace_back(projs, rects, texel_sizes);
	}
	_shadow_slots_info_ssbo.set(shadow_params);
	_lights.flush();
}

[[maybe_unused]] static constexpr glm::vec3 s_cube_face_forward[] = {
	AXIS_X, -AXIS_X,
	AXIS_Y, -AXIS_Y,
	AXIS_Z, -AXIS_Z,
};
[[maybe_unused]] static constexpr glm::vec3 s_cube_face_up[] = {
	-AXIS_Y, -AXIS_Y,
	AXIS_Z, -AXIS_Z,
	-AXIS_Y, -AXIS_Y,
};

std::tuple<glm::mat4, float, float> ShadowAtlas::light_view_projection(const GPULight &light, size_t idx)
{
	// TODO: this is actually only needed if the light has changed. cache it where?

	const auto far_z  = light.affect_radius;
	const auto near_z = std::max(0.1f, far_z / 500.f);

	if(IS_POINT_LIGHT(light))
	{
		assert(idx < 6);
		static constexpr auto square = 1.f;

		const auto &view_forward   = s_cube_face_forward[idx];
		const auto &view_up        = s_cube_face_up[idx];

		assert(glm::epsilonEqual(glm::length(view_forward), 1.f, 0.01f));
		const auto light_view      = glm::lookAt(light.position, light.position + view_forward, view_up);
		const auto face_projection = glm::perspective(glm::half_pi<float>(), square, near_z, far_z);
		const auto light_vp        = face_projection * light_view;

		return { light_vp, near_z, far_z };
	}
	else if(IS_SPOT_LIGHT(light))
	{
		assert(idx == 0);
		static constexpr auto square = 1.f;

		const auto &view_forward = light.direction;
		const auto &view_up      = view_forward == AXIS_Z or view_forward == -AXIS_Z? AXIS_X: AXIS_Z;

		const auto light_view    = glm::lookAt(light.position, light.position + view_forward, view_up);
		const auto projection    = glm::perspective(light.outer_angle*2, square, near_z, far_z);
		const auto light_vp      = projection * light_view;

		return { light_vp, near_z, far_z };
	}
	else
	{
		assert(false);
		// for directional lights, the necessary stuff is calculated in update_csm_params()
	}

	return { glm::mat4(1), -1, -1 };
}


const ShadowAtlas::CSMParams &ShadowAtlas::update_csm_params(LightID light_id, const Camera &camera)//, float radius_uv)
{
	// TODO: move to ShadowAtlas ?
	//   if so, also move view_projection() ?

	auto found = _id_to_allocated.find(light_id);
	if(found == _id_to_allocated.end())
	{
		assert(false);
		return _csm_params;
	}
	const auto &atlas_light = found->second;
	assert(atlas_light.num_slots >= 1 and atlas_light.num_slots <= 6);
	const auto num_cascades = atlas_light.num_slots;
	const auto shadow_map_size = float(atlas_light.slots[0].size);

	const auto &sun = _lights.get_by_id(light_id);

	_csm_params.num_cascades = num_cascades;

	// this bit needs only be done when the frustum's near/far planes has changed
	float near_z     = camera.nearPlane();
	float far_z      = camera.farPlane();   // distance is limited below  (using _max_distance)
	float clip_range = far_z - near_z;
	float ratio      = far_z / near_z;

	auto linear2normalized = [near_z, far_z](float d) {
		return (d - near_z) / (far_z - near_z);
	};

	float last_split_dist  = 0.f;
	// float avg_frustum_size = 0.f;

	const auto range_scale = 1.f;//std::min(_max_distance / far_z, 1.f);

	for(auto cascade = 0u; cascade < num_cascades; ++cascade)
	{
		// split frustum in depth slices; blend between logarithmic and linear
		float p        = float(cascade + 1) / float(num_cascades);
		float d_log    = near_z * std::pow(ratio, p);
		float d_linear = near_z + clip_range * p;
		float d_mix    = glm::mix(d_linear, d_log, _csm_frustum_split_mix);

		float split_dist = linear2normalized(d_mix) * range_scale;

		const auto split_depth = (camera.nearPlane() + split_dist * clip_range) * -1.0f;
		// m_directional_light_shader->setUniform("u_cascade_splits[" + std::to_string(i) + "]", split_depth);
		_csm_params.camera_depth[cascade] = split_depth;


		std::array<glm::vec3, 8> frustum_corners = camera.frustum().corners(); // copy

		auto slice_frustum = [&frustum_corners, last_split_dist](uint32_t idx, float distance) {
			//                                            far   -   near
			const auto to_far_plane  = frustum_corners[idx + 2] - frustum_corners[idx];
			frustum_corners[idx + 2] = frustum_corners[idx] + (to_far_plane * distance);
			frustum_corners[idx]     = frustum_corners[idx] + (to_far_plane * last_split_dist);
		};
		slice_frustum(0, split_dist);  // top-left
		slice_frustum(1, split_dist);  // bottom-left
		slice_frustum(4, split_dist);  // top-right
		slice_frustum(5, split_dist);  // bottom-right

		last_split_dist = split_dist;

		// furstum slice has been calculated -> 'frustum_corners'
		// now, calculate the orthographic projection to encompass the frustum slice

		auto frustum_center = glm::vec3(0);
		for(auto idx = 0u; idx < 8; ++idx)
			frustum_center += frustum_corners[idx];
		frustum_center /= 8.0f;


		float cascade_radius = 0.f;
		std::for_each(frustum_corners.begin(), frustum_corners.end(), [&cascade_radius, frustum_center](const auto &corner) {
			cascade_radius = std::max(cascade_radius, glm::distance(corner, frustum_center));
		});
		cascade_radius = std::ceilf(cascade_radius * 16.0f) / 16.0f;  // not sure why this is a good idea...

		auto light_pos = frustum_center - sun.direction * cascade_radius;
		auto light_view = glm::lookAt(light_pos, frustum_center, AXIS_Y);

		auto light_projection = glm::ortho(-cascade_radius, cascade_radius,
										   -cascade_radius, cascade_radius,
										   0.f, 2.f * cascade_radius);

		auto light_vp = light_projection * light_view;

		// apply "stabilization" logic; to reduce pixel "swimming"
		auto shadow_origin = light_vp * glm::vec4(0, 0, 0, 1);
		shadow_origin *= shadow_map_size / 2.f;

		auto rounded_origin = glm::round(shadow_origin);
		auto round_offset = rounded_origin - shadow_origin;
		round_offset *= 2.f / shadow_map_size;
		round_offset.z = 0.f;
		round_offset.w = 0.f;

		light_projection[3] += round_offset; // inject into projection matrix
		light_vp = light_projection * light_view;


		_csm_params.view[cascade] = light_view;
		_csm_params.view_projection[cascade] = light_vp;

		// Log::debug("   {}: D:{:>5.1f}  C: {:.1f}; {:.1f}; {:.1f}  r: {:.1f}",
		// 		   cascade,
		// 		   -split_depth,
		// 		   frustum_center.x, frustum_center.y, frustum_center.z,
		// 		   cascade_radius
		// 		   );

		_csm_params.depth_range[cascade] = { -cascade_radius, cascade_radius };
	}

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
	Counters counters;
	for(const auto &[light_id, atlas_light]: _id_to_allocated)
	{
		if(remove_allocation(light_id))
			++counters.dropped;
	}
	_id_to_allocated.clear();

//	_dump_changes(counters);
}

ShadowAtlas::Counters ShadowAtlas::prioritize_lights(const std::vector<LightIndex> &relevant_lights, const glm::vec3 &view_pos, const glm::vec3 &view_forward, std::vector<ShadowAtlas::ValueLight> &prioritized)
{
	// calculate "value" for each shadow-casting light

	float strongest_dir_value { -1.f };  // only the strongest dir light may get a shadow allocation
	LightID strongest_dir_id { NO_LIGHT_ID };

	Counters counters;

	for(const auto &light_index: relevant_lights)
	{
		const auto &light = _lights[light_index];

		if(IS_SHADOW_CASTER(light))
		{
			const auto value = light_value(light, view_pos, view_forward);

			const auto light_id = _lights.light_id(LightIndex(light_index));

			if(value > s_min_light_value)
			{
				if(IS_DIR_LIGHT(light) and value > strongest_dir_value)
				{
					strongest_dir_id = light_id;
					strongest_dir_value = value;
				}
				else
					prioritized.emplace_back(value, light_id, SLOT_CONFIG(light));
			}
			else
			{
				// light is not important  (e.g. too far away)
				if(remove_allocation(light_id))
					++counters.dropped;
			}
		}
	}

	if(strongest_dir_value > s_min_light_value)
	{
		// the "sun" should _always_ get a shadow slot
		// CSM  see: https://learnopengl.com/Guest-Articles/2021/CSM
		prioritized.emplace_back(2.f, strongest_dir_id, SlotConfig::Cascaded);
	}

	std::ranges::sort(prioritized, std::greater<>{});

	return counters;
}

ShadowAtlas::Counters ShadowAtlas::apply_desired_slots(const std::vector<AtlasLight> &desired_slots, const Time now)
{
	// std::puts("-- apply_desired_slots()");

	// small_vec<decltype(_shadow_params_ssbo)::value_type, 120> shadow_params;

	Counters counters;

	// size changes must be done in two phases; remember which they were
	small_vec<uint32_t, 120> changed_size;

	std::array<size_t, 4> size_promised = { 0, 0, 0, 0 };  // max to min level slot sizes promised (used for pro/demotions)

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
				// TODO: in the case of demotion, if no slots are available, demute further?

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
		// std::print("; {} remaining\n", _slot_sets[desired.slots[0].size].size());

		atlas_light._last_size_change = now;
		atlas_light._dirty = true;

		found->second = atlas_light;
	}

	// no more promises!  (we already delivered on those above)
	size_promised = { 0, 0, 0, 0 };

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
				Log::error("  [{}] OUT OF SLOTS size {}", light_id, desired.slots[0].size);
				debug_dump_allocated(true);
				Log::error("size_promised: 1024:{} 512:{} 256:{} 128:{}",
						   size_promised[0], size_promised[1], size_promised[2],
						   size_promised[3], size_promised[4], size_promised[5]);
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
				const auto node_index = alloc_slot(atlas_light.slots[idx].size);

				atlas_light.slots[idx].node_index = node_index;
				atlas_light.slots[idx].rect = mk_rect(_allocator.rect(node_index), 1);
			}
			// std::print("; {} remaining\n", _slot_sets[atlas_light.slots[0].size].size());

			_id_to_allocated[light_id] = atlas_light;

			if(desired.slot_config == SlotConfig::Cascaded)
				_sun_id = light_id;
		}
	}

	return counters;
}

bool ShadowAtlas::has_slots_available(const AtlasLight &atlas_light, const std::array<size_t, 4> &size_promised) const
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
		const auto promised = size_promised[slot_size_idx(ss.size)];
		const auto num_free = _slot_sets.find(ss.size)->second.size();
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

ShadowAtlas::SlotID ShadowAtlas::alloc_slot(SlotSize size, bool first)
{
	assert(_slot_sets.contains(size));

	auto &free_slots = _slot_sets[size];
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

	// std::print("    alloc {:>4} -> {}    rem: {}\n", size, node_index, free_slots.size());

	return node_index;
}

void ShadowAtlas::free_slot(SlotSize size, SlotID node_index)
{
	assert(_slot_sets.contains(size));

	auto &free_slots = _slot_sets[size];
	assert(free_slots.capacity() > free_slots.size()); // i.e. should never grow beyond its original size

	// std::print("     free {:>4} -- {}    rem: {}\n", size, node_index, free_slots.size());

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

float ShadowAtlas::light_value(const GPULight &light, const glm::vec3 &view_pos, const glm::vec3 &view_forward) const
{
	// calculate the "value" of a light on a fixed scale  [0, 1]

	assert(_max_distance > 0);

	if(IS_DIR_LIGHT(light))  // i.e. the sun, which is always relevant
		return 1.f;

	const auto edge_distance = std::max(0.f, glm::distance(light.position, view_pos) - light.affect_radius);
	if(edge_distance >= _max_distance) // too far away
		return 0.f;

	const auto normalized_dist = edge_distance / _max_distance;
	// nrmalize the radius using a "large" radius
	const auto normalized_radius = std::min(light.affect_radius / _large_light_radius, 1.f);

	const auto importance = std::min(1.2f * normalized_radius / glm::max(normalized_dist, 1e-4f), 1.f);
	const auto base_weight = importance * importance; // inverse square falloff

	const auto type_weight = 1.f; // e.g. 0.8f for point, 1.f for spot, etc.
	float facing_weight = 1.f;
	if(edge_distance > 0)  // i.e. outside  light affect radius
	{
		// outside the light's radius decrease based on facing angle

		static const auto cutoff = std::cos(glm::radians(45.f)); // start decrease at 45 degrees
		static const auto min_dot = 0.f;

		const auto facing = glm::dot(glm::normalize(light.position - view_pos), view_forward);
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

void ShadowAtlas::generate_slots(std::initializer_list<size_t> distribution)
{
	const auto T0 = steady_clock::now();

	// size of 'distribution' should match number of allocatable levels - 1
	assert(distribution.size() == _allocator.num_allocatable_levels() - 1);

	_distribution.assign(distribution.begin(), distribution.end());   // max_size -> min_size (last is calculated below)

	_slot_sets.reserve(_allocator.num_allocatable_levels());
	_slot_sets.clear();

	_total_num_slots = 0;

	// use the allocator to calculate how many slots are possible
	//  e.g. allocate the first N-1 levels of the distribution,
	//  than ask the allocator to count how many of the last size there is room left for
	auto slot_size = _allocator.max_size();

	for(const auto count: _distribution)
	{
		auto &free_slots = _slot_sets[slot_size];
		free_slots.clear();
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
	}

	// then the remaining space is allocated using the smallest size
	auto &free_slots = _slot_sets[slot_size];
	// unfortunately we don't know how many there will be, but guessing plenty :)
	free_slots.reserve(_distribution[_distribution.size() - 2] << 1);
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

	_distribution.push_back(free_slots.size());

	const auto Td = steady_clock::now() - T0;

	Log::info("\x1b[32;1mShadowAtlas\x1b[m {} shadow map slots defined, in {}", _total_num_slots, duration_cast<microseconds>(Td));
	slot_size = _allocator.max_size();
	for(const auto count: _distribution)
	{
		Log::info("  {:>4}: {} slots", slot_size, count);
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
