#include "shadow_atlas.h"

#include <glm/vec3.hpp>

#include "light_constants.h"
#include "light_manager.h"

#include <chrono>
#include <ranges>
#include <string_view>
#include <print>
#include <cstdio>

#include "buffer_binds.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/epsilon.hpp"
#include "constants.h"

using namespace std::literals;

static constexpr float s_min_light_value = 1e-2f;


namespace std
{
template<>
struct hash<glm::vec3>
{
	size_t operator()(const glm::vec3 &v) const
	{
	  // -0.0 and 0.0 should return same hash
		return std::hash<float>()(v.x) ^ std::hash<float>()(v.y) ^ std::hash<float>()(v.z);
	}
};
} // std

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

#define NUM_SHADOW_SLOTS(light)   (IS_POINT_LIGHT(light)? 6u: (IS_DIR_LIGHT(light)? 3: 1u))


using namespace std::chrono;
using namespace std::literals;

inline glm::uvec4 to_uvec4(const RGL::SpatialAllocator<uint32_t>::Rect &r)
{
	return glm::uvec4(r.x, r.y, r.w, r.h);
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
	_allocator(size, size >> (s_slot_max_size_shift + 3), size >> s_slot_max_size_shift)
{
	size = std::bit_ceil(size);
	assert(size >= 1024 and size <= 16384);

	_shadow_slots_info_ssbo.bindAt(SSBO_BIND_SHADOW_SLOTS_INFO);

	_distribution.reserve(4);
	generate_slots({ 24 + 1, 64 + 1, 256 + 1 });  // +1 for directional/sun light

	// TODO: these sohould be configurable
	_render_intervals.push_back({ 0, 0ms });
	_render_intervals.push_back({ 1, 25ms });
	_render_intervals.push_back({ 2, 50ms });
	_render_intervals.push_back({ 4, 100ms });

	// set aside 3 sots to sun light (uses CSM)
	// will be used by the (strongest) directional light
	// TODO: try to avoid hard-coding this allocation
	//   seem slike a waste if there's actually no sun light present.
	_allocated_sun.uuid = NO_LIGHT_ID;
	_allocated_sun.num_slots = 3;
	static constexpr bool LastSlot = false;
	const auto start_size = size >> s_slot_max_size_shift;
	for(auto idx = 0u; idx < _allocated_sun.num_slots; ++idx)
	{
		const auto slot_size = start_size >> idx;
		const auto node_index = alloc_slot(slot_size, LastSlot);

		_allocated_sun.slots[idx].size = slot_size;
		_allocated_sun.slots[idx].node_index = node_index;
		_allocated_sun.slots[idx].rect = to_uvec4(_allocator.rect(node_index));
	}
	_allocated_sun._dirty = true;
}

ShadowAtlas::~ShadowAtlas()
{
	release();
}

bool ShadowAtlas::create()
{
	const auto size = _allocator.size();

	namespace C = RGL::RenderTarget::Color;
	namespace D = RGL::RenderTarget::Depth;
	// store 2-omponent normals as well as depth
	Texture2d::create("shadow-atlas", size, size, C::Texture | C::Float2, D::Texture | D::Float);
	// TODO: if we only use the color attachment (i.e. the normals) for slope comparison,
	//   we really only need a single-channel float (basically the cos(light_to_fragment_angle)).

	// enable usage of sampler2DShadow in GLSL
	enableHardwarePCF();

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

	// 2. "pour" the valued lights into the sizd -buckets.
	//    NOTE: this is just a "desire"; not affected by already allocated slots

	static std::vector<AtlasLight> desired_slots;
	desired_slots.reserve(std::max(64ul, prioritized.size()));
	desired_slots.clear();

	small_vec<size_t, 8> distribution(_distribution.begin(), _distribution.end());

	// std::print("  start_size_idx: {}   top value: {}  [{}]\n", size_idx, top_light_value, prioritized.begin()->light_id);

	for(const auto &prio_light: prioritized)
	{
		AtlasLight atlas_light;
		atlas_light.uuid      = prio_light.light_id;
		atlas_light.num_slots = prio_light.num_slots;

		// based on the light's value, deduce where to start searching for available slots
		auto size_idx = static_cast<uint32_t>(std::floor(static_cast<float>(distribution.size()) * (1 - prio_light.value)));
		// initial position and corresponding slot size
		auto slot_size = _allocator.max_size() >> size_idx;

		// find a slot size  tier that still has room for required slots
		while(distribution[size_idx] < atlas_light.num_slots and size_idx < distribution.size())
		{
			// nothing available, next size
			++size_idx;
			slot_size >>= 1;
		}
		if(size_idx < distribution.size())
		{
			// define the desired slot sizes
			for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
				atlas_light.slots[idx].size = slot_size;

			desired_slots.push_back(atlas_light);
			distribution[size_idx] -= atlas_light.num_slots;
			// std::print("  [{:>2}] desired {} {:>4} slots  -> {:>3} remaining\n",
			// 		   atlas_light.uuid, atlas_light.num_slots, slot_size, distribution[size_idx]);
		}
		else
		{
			// no slots available, remove any previous allocation
			std::print(" [{}] can't fit {} slots\n", atlas_light.uuid, atlas_light.num_slots);
			if(remove_allocation(prio_light.light_id))
				++counters.dropped;
			else
				++counters.denied;
		}
	}

	// _dump_desired(desired_slots);

	// 3. apply the desired slots; actually allocate the slots & assign to the AtlasLight entry
	counters += apply_desired_slots(desired_slots, T0);

	const auto num_changes = counters.changed();

	if(num_changes)
	{
		std::print("\x1b[32;1mShadowAtlas\x1b[m {} lights ->", prioritized.size());
		if(counters.retained)
			std::print(" \x1b[1m=\x1b[m{}", counters.retained);
		if(counters.allocated)
			std::print(" \x1b[33;1m⭐\x1b[m{}", counters.allocated);
		if(counters.dropped)
			std::print(" \x1b[31;1m❌\x1b[m{}", counters.dropped);
		if(counters.denied)
			std::print(" \x1b[31;1m!\x1b[m{}", counters.denied);
		if(counters.promoted)
			std::print(" \x1b[32;1m🡅\x1b[m{}", counters.promoted);
		if(counters.demoted)
			std::print(" \x1b[34;1m🡇\x1b[m{}", counters.demoted);
		if(counters.change_pending)
			std::print(" \x1b[1m❔\x1b[m{}", counters.change_pending);
		std::print(", in {} ->", duration_cast<microseconds>(steady_clock::now() - T0));
#if defined(DEBUG)
		debug_dump_allocated(false);
#endif
		std::puts("");
	}

	// return how many shadow maps changed  (new, dropped, promoted, demoted)
	return num_changes;
}

size_t ShadowAtlas::hash_light(const GPULight &L) const
{
	static std::hash<float> fH;
	static std::hash<glm::vec3> vH;

	switch(GET_LIGHT_TYPE(L))
	{
	case LIGHT_TYPE_POINT:
		return vH(L.position) ^ fH(L.affect_radius);
	case LIGHT_TYPE_DIRECTIONAL:
		return vH(L.direction);
	case LIGHT_TYPE_SPOT:
		return vH(L.position)
			^ fH(L.spot_bounds_radius)  // i.e. affect_radius and outer_angle
			^ vH(L.direction);
	case LIGHT_TYPE_AREA:
	case LIGHT_TYPE_DISC:
	case LIGHT_TYPE_TUBE:
	case LIGHT_TYPE_SPHERE:
		// not shadow casters (currently)
		return 0;
	}

	return 0;
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

	for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
	{
		const auto &slot = atlas_light.slots[idx];
		free_slot(slot.size, slot.node_index);
	}

	_id_to_allocated.erase(found);
	_lights.clear_shadow_index(light_id);

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
		auto slot_size = atlas_light.slots[0].size;
		auto found = size_counts.find(slot_size);
		if(found == size_counts.end())
		{
			sizes.push_back(slot_size);
			size_counts[slot_size] = 1;
		}
		else
			++found->second;

		if(details)
		{
			std::print("  - {:3}  {:2} slots; shadow idx: [{}]\n", light_id, atlas_light.num_slots, _lights.shadow_index(light_id));
			std::array<size_t, 4> alloc_counts = { 0, 0, 0, 0 };
			for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
				++alloc_counts[slot_size_idx(atlas_light.slots[idx].size)];
			std::print("        sizes:");
			for(const auto &[level, count]: std::views::enumerate(alloc_counts))
			{
				if(count)
					std::print(" {:>4} {}", 1024 >> level, count);
			}
			std::puts("");
		}
	}
	if(not sizes.empty())
	{
		std::ranges::sort(sizes, std::greater<SlotSize>());
		std::print(" {{ ");
		auto first = true;
		for(const auto &slot_size: sizes)
		{
			if(not first)
				std::print(", ");
			first = false;
			std::print("{}:{}", slot_size, size_counts[slot_size]);
		}
		std::print(" }}\n");

		auto num_available = 0u;
		for(const auto &[size, slot_set]: _slot_sets)
			num_available += slot_set.size();

		assert(num_available + num_used + 3 == _max_shadow_slots);  // 3 = CSM slots for directional light
	}
}

void ShadowAtlas::debug_dump_desired(const std::vector<AtlasLight> &desired_slots) const
{
	std::print("=== Desired slots ({}):\n", desired_slots.size());
	for(const auto &atlas_light: desired_slots)
	{
		std::print("  - {:3}  {:2} slots\n", atlas_light.uuid, atlas_light.num_slots);
		std::array<size_t, 4> alloc_counts = { 0, 0, 0, 0 };
		for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
			++alloc_counts[slot_size_idx(atlas_light.slots[idx].size)];
		std::print("        sizes:");
		for(const auto &[level, count]: std::views::enumerate(alloc_counts))
		{
			if(count)
				std::print(" {:>4} {}", 1024 >> level, count);
		}
		std::puts("");
	}
}


static glm::mat4 light_view_projection(const GPULight &light, size_t idx=0);

void ShadowAtlas::update_shadow_params()
{
	static std::vector<decltype(_shadow_slots_info_ssbo)::value_type> shadow_params;
	shadow_params.reserve(_id_to_allocated.size());
	shadow_params.clear();

	for(auto &[light_id, atlas_light]: allocated_lights())
	{
		const auto &light = _lights.get_by_id(light_id);

		std::array<glm::mat4, 6> projs;
		std::array<glm::uvec4, 6> rects;

		for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
		{
			projs[idx] = light_view_projection(light, idx);
			rects[idx] = atlas_light.slots[idx].rect;
		}

		_lights.set_shadow_index(light_id, shadow_params.size());

		shadow_params.emplace_back(projs, rects);
	}

	_shadow_slots_info_ssbo.set(shadow_params);
	_lights.flush();
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

	float strongest_dir_value { -1.f };

	Counters counters;

	for(const auto &light_index: relevant_lights)
	{
		const auto &light = _lights[light_index];

		if(IS_SHADOW_CASTER(light))
		{
			// const auto light_id = lights.light_id(light_index);
			// std::print("  [{}] ", light_id);
			const auto value = light_value(light, view_pos, view_forward);

			const auto light_id = _lights.light_id(LightIndex(light_index));

			if(value > s_min_light_value)
			{
				if(IS_DIR_LIGHT(light) and value > strongest_dir_value)
				{
					_allocated_sun.uuid = light_id;
					strongest_dir_value = value;
				}
				else
					prioritized.emplace_back(value, light_id, NUM_SHADOW_SLOTS(light));
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
		prioritized.push_back({
			.value = 2.f,          // light should _always_ be included
			.light_id = _allocated_sun.uuid,
			.num_slots = 3,        // CSM  see: https://learnopengl.com/Guest-Articles/2021/CSM
		});
	}

	std::ranges::sort(prioritized, std::greater<>{});

	if(_allocated_sun.uuid != NO_LIGHT_ID)
		assert(IS_DIR_LIGHT(_lights.get_by_id(prioritized[0].light_id)));

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

	// deallocate for pro/demotions first, then new allocations and allocations for pro/demotions

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
			slot.rect = to_uvec4(_allocator.rect(node_index));
		}
		// std::print("; {} remaining\n", _slot_sets[desired.slots[0].size].size());

		atlas_light._last_size_change = now;
		atlas_light._dirty = true;

		found->second = atlas_light;
	}

	// no more promises!  (we already did those above)
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
				std::print("  [{}] OUT OF SLOTS size {}\n", light_id, desired.slots[0].size);
				debug_dump_allocated(true);
				std::print("size_promised: 1024:{} 512:{} 256:{} 128:{}\n",
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
				atlas_light.slots[idx].rect = to_uvec4(_allocator.rect(node_index));
			}
			// std::print("; {} remaining\n", _slot_sets[atlas_light.slots[0].size].size());

			_id_to_allocated[light_id] = atlas_light;
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

void ShadowAtlas::generate_slots(std::initializer_list<uint32_t> distribution)
{
	const auto T0 = steady_clock::now();

	// size of 'distribution' should match number of allocatable levels - 1
	assert(distribution.size() == _allocator.num_allocatable_levels() - 1);

	_distribution.assign(distribution.begin(), distribution.end());   // max_size -> min_size (last is calculated below)

	_slot_sets.reserve(_allocator.num_allocatable_levels());
	_slot_sets.clear();

	_max_shadow_slots = 0;

	// use the allocator to calculate how many slots are possible
	//  e.g. allocate the first four levels of the distribution,
	//  than ask the allocator to count how many of the last size there is room for
	auto size = _allocator.max_size();

	for(const auto count: _distribution)
	{
		auto &free_slots = _slot_sets[size];
		free_slots.reserve(count);

		for(auto idx = 0u; idx < count; ++idx)
		{
			const auto index = _allocator.allocate(size);
			assert(index != _allocator.end());
			// prepend so they're allocated in "correct" order (uses pop_back())
			free_slots.insert(free_slots.begin(), index);
		}
		_max_shadow_slots += free_slots.size();

		size >>= 1;
	}

	// then the remaining space is allocated using the smallest size
	auto &free_slots = _slot_sets[size];
	// unfortunately we don't know how many there will be,
	//   but at least twice as many as the previous size
	free_slots.reserve(_distribution[_distribution.size() - 2] << 1);
	do
	{
		const auto index = _allocator.allocate(size);
		if(index != _allocator.end())
			free_slots.push_back(index);
		else
			break;
	}
	while(true);

	_max_shadow_slots += free_slots.size();

	_distribution.push_back(free_slots.size());

	const auto Td = steady_clock::now() - T0;

	std::print("ShadowAltas: {} shadow map slots defined, in {}\n", _max_shadow_slots, duration_cast<microseconds>(Td));
}

#include <cmath> // std::signbit

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



[[maybe_unused]] static constexpr std::string_view face_names[] = {
	"+X"sv, "-X"sv,
	"+Y"sv, "-Y"sv,
	"+Z"sv, "-Z"sv,
};

static glm::mat4 light_view_projection(const GPULight &light, size_t idx)
{
	const auto far_z  = light.affect_radius;
	const auto near_z = std::max(0.1f, far_z / 250.f);

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

		// std::print("  point VP {} ->\n{: .3f}\n", face_names[idx], light_view);

		return light_vp;
	}
	if(IS_DIR_LIGHT(light))
	{
		assert(idx < 3);
		// TODO
		//   3 cascades (CSM)
	}
	if(IS_SPOT_LIGHT(light))
	{
		assert(idx == 0);
		static constexpr auto square = 1.f;

		const auto &view_forward = light.direction;
		const auto &view_up      = view_forward == AXIS_Z or view_forward == -AXIS_Z? AXIS_X: AXIS_Z;

		const auto light_view    = glm::lookAt(light.position, light.position + view_forward, view_up);
		const auto projection    = glm::perspective(light.outer_angle*2, square, near_z, far_z);
		const auto light_vp      = projection * light_view;

		return light_vp;
	}

	return glm::mat4(1);
}

ShadowAtlas::AtlasLight::AtlasLight(const AtlasLight &other) :
	uuid(other.uuid),
	num_slots(other.num_slots),
	slots(other.slots),
	hash(0),
	_dirty(true),
	_frames_skipped(0)
{
}
