#include "shadow_atlas.h"

#include <glm/vec3.hpp>

#include "light_constants.h"
#include "light_manager.h"

#include <chrono>
#include <ranges>
#include <algorithm>
#include <string_view>
#include <print>
#include <cstdio>

#include "buffer_binds.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "constants.h"

using namespace std::literals;


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

[[maybe_unused]] static const std::string_view _light_type_names[] {
	"point"sv,
	"directional"sv,
	"spot"sv,
	"area"sv,
	"tube"sv,
	"sphere"sv,
	"disc"sv,
};
#define LIGHT_TYPE_NAME(light)   (_light_type_names[(light.type_flags & LIGHT_TYPE_MASK)])

#define LIGHT_NUM_SLOTS(light)   (IS_POINT_LIGHT(light)? 6u: (IS_DIR_LIGHT(light)? 3: 1u))


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

ShadowAtlas::ShadowAtlas(uint32_t size) :
	Texture2d(),
	_allocator(size, size >> 6, size >> 3),
	_min_change_interval(1s),
	_shadow_params_ssbo("shadow-params")
{
	if(__builtin_popcount(size) != 1)
		size = 1 << (sizeof(size)*8 -  size_t(__builtin_clz(size)));  // round up to next Po2
	assert(size >= 1024 and size <= 16384);

	_shadow_params_ssbo.setBindIndex(SSBO_BIND_SHADOW_PARAMS);

	_distribution.reserve(4);
	generate_slots({ 24 + 1, 64 + 1, 256 + 1 });  // +1 for directional/sun light


	// set aside 3 sots to sun light (uses CSM)
	// will be used by the (strongest) directional light
	_allocated_sun.uuid = NO_LIGHT_ID;
	_allocated_sun.num_slots = 3;
	for(auto idx = 0u; idx < _allocated_sun.num_slots; ++idx)
	{
		_allocated_sun.slots[idx].size = size >> (3 + idx);
		_allocated_sun.slots[idx].node_index = alloc_slot(_allocated_sun.slots[idx].size);
		_allocated_sun.slots[idx].rect = to_uvec4(_allocator.rect(_allocated_sun.slots[idx].node_index));
	}
	_allocated_sun._dirty = true;
	_allocated_sun.last_rendered = steady_clock::now() - 1h;  // to "guarantee" rendering immediately
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

	return bool(this);
}

size_t ShadowAtlas::eval_lights(LightManager &lights, const glm::vec3 &view_pos, const glm::vec3 &view_forward)
{
	const Time T0 = steady_clock::now();

	static std::vector<ValueLight> prioritized;
	prioritized.reserve(lights.size());
	prioritized.clear();

	float strongest_dir_value { -1.f };

	auto num_dropped = 0u; // deallocated lights

	// calculate "value" for each shadow-casting light
	LightIndex light_index = 0;  // needed to retrieve the LightID
	for(const auto &light: lights)
	{
		if(IS_SHADOW_CASTER(light))
		{
			// const auto light_id = lights.light_id(light_index);
			// std::print("  [{}] ", light_id);
			const auto value = light_value(light, view_pos, view_forward);

			if(value > 0)
			{
				const auto light_id = lights.light_id(light_index);

				if(IS_DIR_LIGHT(light) and value > strongest_dir_value)
				{
					_allocated_sun.uuid = light_id;
					strongest_dir_value = value;
				}
				else
				{
					prioritized.push_back({
						.value = value,
						.light_id = light_id,
						.num_slots = LIGHT_NUM_SLOTS(light),
					});
				}
			}
			else
			{
				const auto light_id = lights.light_id(light_index);

				// light has no value  (e.g. too far away)
				if(remove_allocation(light_id))
				{
					++num_dropped;
					lights.clear_shadow_index(light_id);
				}
			}
		}

		++light_index;
	}

	if(strongest_dir_value > -1.f)
	{
		prioritized.push_back({
			.value = 2.f,  // should *always* be included
			.light_id = _allocated_sun.uuid,
			.num_slots = 3,   // CSM  see: https://learnopengl.com/Guest-Articles/2021/CSM
		});
	}

	std::ranges::sort(prioritized, [](const auto &A, const auto &B) {
		return A.value > B.value;
	});
	if(_allocated_sun.uuid != NO_LIGHT_ID)
		assert(IS_DIR_LIGHT(lights.get_by_id(prioritized[0].light_id).value().get()));

	// count how many lights we have slots for
	// TODO: this is not accurate, does not take different slot sizes into account
	//    i.e. a given light must be allocated only same-sized slots
	auto needed_slots = 0u;
	auto space_for_lights = 0u;
	for(const auto &prio_light: prioritized)
	{
		if(prio_light.num_slots + needed_slots > _max_shadow_slots)
			break;
		needed_slots += prio_light.num_slots;
		++space_for_lights;
	}

	// the rest: no soup for you!
	if(space_for_lights < prioritized.size())
	{
		std::print("{} lights w/o shadow slot -> {} remaining\n", prioritized.size() - space_for_lights, space_for_lights);

		for(const auto &prio: std::ranges::subrange(prioritized.begin() + ptrdiff_t(space_for_lights), prioritized.end()))
		{
			// free the previous slot (if any)
			if(remove_allocation(prio.light_id));
			{
				++num_dropped;
				lights.clear_shadow_index(prio.light_id);
			}
		}

		// cut off excess lights
		prioritized.resize(space_for_lights);
	}

	// "pour" the valued lights into the size-buckets.
	//   the top light's value determines which size to start at.
	//   the rest follows in value order, with decreasing sizes as distribution allows.

	small_vec<AtlasLight, 120> desired_slots;

	// const auto top_light_value = prioritized.begin()->value;
	// value = 1 is the most important possible; gets the highest-resolution slot
	small_vec<size_t, 8> distribution(_distribution.begin(), _distribution.end());

	// std::print("  start_size_idx: {}   top value: {}  [{}]\n", size_idx, top_light_value, prioritized.begin()->light_id);


	// TODO: directional lights, using CSM. see: https://learnopengl.com/Guest-Articles/2021/CSM
	//   might be reasonable to dedicate 3 slots to it; 1024, 512, 256, straight up

	// "drop" the light at the top of 'distribution' (start size)
	// allotting where there's first space.

	for(const auto &prio_light: prioritized)
	{
		const auto light_ = lights.get_by_id(prio_light.light_id);
		assert(light_.has_value());
		const auto &light = light_.value().get();

		AtlasLight atlas_light;
		atlas_light.uuid      = prio_light.light_id;
		atlas_light.num_slots = LIGHT_NUM_SLOTS(light);

		// calculate where to start searching for slots, based on the light's value
		const auto size_idx = static_cast<uint32_t>(std::floor(static_cast<float>(distribution.size()) * (1 - prio_light.value)));
		// initial position and corresponding slot size
		auto iter = distribution.begin() + size_idx;
		auto slot_size = _allocator.max_size() >> size_idx;

		while(*iter < atlas_light.num_slots and iter != distribution.end())
		{
			// nothing availablem, next size
			++iter;
			slot_size >>= 1;
		}
		if(iter != distribution.end())
		{
			// declare the desired slot sizes

			for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
				atlas_light.slots[idx].size = slot_size;

			desired_slots.push_back(atlas_light);
			*iter -= atlas_light.num_slots;
		}
		else
		{
			// no slots available
			if(remove_allocation(prio_light.light_id))
			{
				++num_dropped;
				lights.clear_shadow_index(prio_light.light_id);
			}
		}
	}

	// _dump_desired(desired_slots);

	const auto counters = apply_desired_slots(lights, desired_slots, T0);

	const auto num_changes = counters.allocated + num_dropped + counters.promoted + counters.demoted;

	if(num_changes)
	{
		std::print("ShadowAtlas: {} -> new: {} kept: {} dropped: {} promote: {} demote: {} (pending: {}), in {}\n",
				   prioritized.size(),
				   counters.allocated,
				   counters.retained,
				   num_dropped,
				   counters.promoted,
				   counters.demoted,
				   counters.change_pending,
				   duration_cast<microseconds>(steady_clock::now() - T0));
		_dump_allocated_counts();
	}

	// return how many shadow maps changed  (new, dropped, promoted, demoted)
	return num_changes;
}

size_t ShadowAtlas::light_hash(const GPULight &L) const
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

	return true;
}

void ShadowAtlas::_dump_desired(const small_vec<ShadowAtlas::AtlasLight, 120> &desired_slots)
{
	std::print("--- Desired slots ({}):\n", desired_slots.size());
	for(const auto &desired: desired_slots)
	{
		std::print("  [{}] size: {}  x{}\n", desired.uuid, desired.slots[0].size, desired.num_slots);
	}
}

void ShadowAtlas::_dump_allocated_counts()
{
	static dense_map<SlotSize, size_t> size_counts;
	if(size_counts.empty())
		size_counts.reserve(_distribution.size());
	size_counts.clear();
	static std::vector<SlotSize> sizes;
	if(sizes.capacity() == 0)
		sizes.reserve(_distribution.size());
	sizes.clear();

	for(const auto &[light_id, allocated]: _id_to_allocated)
	{
		auto slot_size = allocated.slots[0].size;
		auto found = size_counts.find(slot_size);
		if(found == size_counts.end())
		{
			sizes.push_back(slot_size);
			size_counts[slot_size] = 1;
		}
		else
			++found->second;
	}
	if(not sizes.empty())
	{
		std::ranges::sort(sizes);
		std::print("  sizes:");
		for(const auto &slot_size: sizes)
			std::print("  {}:{}", slot_size, size_counts[slot_size]);
		std::puts("");
	}
}

static glm::mat4 light_view_projection(const GPULight &light, size_t idx=0);

ShadowAtlas::ApplyCounters ShadowAtlas::apply_desired_slots(LightManager &lights, const small_vec<AtlasLight, 120> &desired_slots, const Time now)
{
	// std::puts("-- apply_desired_slots()");

	small_vec<decltype(_shadow_params_ssbo)::value_type, 120> shadow_params;

	auto add_params = [&shadow_params, &lights](LightID light_id, const std::array<glm::uvec4, 6> &rects) {
		auto light_ = lights.get_by_id(light_id);
		auto &light = light_.value().get();

		std::array<glm::mat4, 6> projs;
		const size_t num_projs = LIGHT_NUM_SLOTS(light);
		for(auto idx = 0u; idx < num_projs; ++idx)
			projs[idx] = light_view_projection(light, idx);

		shadow_params.push_back(LightShadowParams {
			.view_proj = projs,
			.atlas_rect = rects,
		});

		lights.set_shadow_index(light_id, uint_fast16_t(shadow_params.size() - 1));
	};

	auto num_retained = 0u;
	auto num_allocated = 0u;
	auto num_change_pending = 0u;
	auto num_promoted = 0u;
	auto num_demoted = 0u;

	// size changes must be done in two phases; remember which they were
	small_vec<uint32_t, 120> changed_size;
	auto desired_index = 0u;

	// (re)allocate slots according to declared desire
	for(const auto &desired: desired_slots)
	{
		const auto light_id = desired.uuid;

		std::array<glm::uvec4, 6> rects; // rects to be stored in the shadow params SSBO

		auto found = _id_to_allocated.find(light_id);
		if(found == _id_to_allocated.end())
		{
			// new shadow slot
			++num_allocated;

			auto atlas_light = desired;

			// std::print("  [{}] alloc {} slots:   {}", light_id, atlas_light.num_slots, atlas_light.slots[0].size);
			// std::fflush(stdout);

			for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
			{
				const auto node_index = alloc_slot(atlas_light.slots[idx].size);

				atlas_light.slots[idx].node_index = node_index;
				atlas_light.slots[idx].rect = to_uvec4(_allocator.rect(node_index));
				rects[idx] = atlas_light.slots[idx].rect;
			}
			// std::print("; {} remaining\n", _slot_sets[atlas_light.slots[0].size].size());

			atlas_light._dirty = true;
			_id_to_allocated[light_id] = atlas_light;
		}
		else
		{
			// was allocated before, compare it
			auto &atlas_light = found->second;

			const auto size_diff = int32_t(desired.slots[0].size) - int32_t(atlas_light.slots[0].size);
			const auto change_age = now - atlas_light.last_size_change;

			const auto has_slots = slots_available(desired);

			if(size_diff == 0 or change_age < _min_change_interval or not has_slots)
			{
				++num_retained;

				if(size_diff != 0)
					++num_change_pending;

				// no change to _id_to_allocated
				for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
					rects[idx] = atlas_light.slots[idx].rect;

				add_params(light_id, rects);
			}
			else
			{
				changed_size.push_back(desired_index);

				// first deallocate demotions and promotions first,
				//   then the corresponding allocation

				if(size_diff > 0)
					++num_promoted;
				else
					++num_demoted;

				// free the previous size slot to the pool
				// std::print("  [{}]  free {} slots:    {}: {} (-> {})",
				// 		   light_id, atlas_light.num_slots, size_diff > 0?"pro":"dem", atlas_light.slots[0].size, desired.slots[0].size);
				std::fflush(stdout);
				auto idx = atlas_light.num_slots;  // in reverse to "put back" in the same order
				while(idx-- != 0)
				{
					const auto &slot = atlas_light.slots[idx];
					free_slot(slot.size, slot.node_index);
				}
				// std::print("; {} remaining\n", _slot_sets[atlas_light.slots[0].size].size());
			}
		}

		++desired_index;
	}

	// allocate the slots that changed size
	for(const auto index: changed_size)
	{
		const auto &desired = desired_slots[index];
		const auto light_id = desired.uuid;

		std::array<glm::uvec4, 6> rects;

		auto found = _id_to_allocated.find(light_id);
		assert(found != _id_to_allocated.end());

		auto &atlas_light = found->second;

		// std::print("  [{}] alloc {} slots:   {}", light_id, atlas_light.num_slots, desired.slots[0].size);
		// std::fflush(stdout);

		for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
		{
			// get the new slots from the desired slot size
			auto node_index = alloc_slot(desired.slots[idx].size);
			atlas_light.slots[idx].node_index = node_index;

			atlas_light.slots[idx].size = desired.slots[idx].size;
			atlas_light.slots[idx].rect = to_uvec4(_allocator.rect(node_index));
			rects[idx] = atlas_light.slots[idx].rect;
		}
		// std::print("; {} remaining\n", _slot_sets[desired.slots[0].size].size());

		atlas_light.last_size_change = now;
		atlas_light._dirty = true;

		_id_to_allocated[light_id] = atlas_light;

		add_params(light_id, rects);
	}

	_shadow_params_ssbo.set(shadow_params);

	return { num_allocated, num_retained, num_promoted, num_demoted, num_change_pending };
}

bool ShadowAtlas::slots_available(const AtlasLight &atlas_light) const
{
	// TODO: check if slots from 'beg' to 'end' are available to allocate
	auto need1_size = 0u;
	auto need1 = 0u;
	auto need2_size = 0u;
	auto need2 = 0u;
	auto need3_size = 0u;
	auto need3 = 0u;

	for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
	{
		auto slot_size = atlas_light.slots[idx].size;

		if(need1_size == 0)
		{
			need1_size = slot_size;
			++need1;
		}
		else if(need1_size == slot_size)
			++need1;
		else if(need2_size == 0)
		{
			need2_size = slot_size;
			++need2;
		}
		else if(need2_size == slot_size)
			++need2;
		else if(need3_size == 0)
		{
			need3_size = slot_size;
			++need3;
		}
		else if(need3_size == slot_size)
			++need3;
	}

	if(_slot_sets.find(need1_size)->second.size() < need1)
		return false;

	if(need2_size == 0)
		return true;
	if(_slot_sets.find(need2_size)->second.size() < need2)
		return false;

	if(need3_size == 0)
		return true;
	if(_slot_sets.find(need3_size)->second.size() < need3)
		return false;

	return true;
}

ShadowAtlas::SlotID ShadowAtlas::alloc_slot(SlotSize size)
{
	assert(_slot_sets.contains(size));

	auto &free_slots = _slot_sets[size];
	assert(not free_slots.empty());

	auto node_index = free_slots.back();
	free_slots.pop_back();

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
	static const auto clear_depth = 0.f;
	glClearTexSubImage(depth_texture().texture_id(),
					   0,                   // mip level
					   GLint(rect.x), GLint(rect.y), 0, // offset
					   GLsizei(rect.w), GLsizei(rect.h), 1, // size
					   GL_DEPTH_COMPONENT,  // format
					   GL_FLOAT,            // type
					   &clear_depth);   // pointer to depth value (e.g. 1.0f)
#endif

	free_slots.push_back(node_index);
}

float ShadowAtlas::light_value(const GPULight &light, const glm::vec3 &view_pos, const glm::vec3 &view_forward) const
{
	// calculate the "value" of a light on a fixed scale  [0, 1]

	assert(_max_distance > 0);

	if(IS_DIR_LIGHT(light))  // i.e. the sun, which is always relevant
		return 1.f;

	const auto distance = glm::distance(light.position, view_pos);
	if(distance >= _max_distance) // too far away
		return 0.f;

	const auto normalized_dist = distance / _max_distance;
	const auto normalized_radius = light.affect_radius / _large_light_radius;

	const auto importance = glm::min(1.2f * normalized_radius / glm::max(normalized_dist, 1e-4f), 1.f);
	const auto base_weight = importance * importance; // inverse square falloff

	const auto type_weight = 1.f; // e.g. 0.8f for point, 1.f for spot, etc.

	float facing_weight = 1.f;
	if(distance > light.affect_radius)
	{
		static const auto cutoff = std::cos(glm::radians(45.f));
		static const auto min_dot = 0.f;

		const auto facing = glm::dot(glm::normalize(light.position - view_pos), view_forward);
		if(facing < cutoff)
		{
			facing_weight = glm::clamp((facing - min_dot) / (cutoff - min_dot), 0.f, 1.f);
			facing_weight = 0.5f + 0.5f * facing_weight; // scales from 0.5 (behind) to 1.f (in front)
		}
	}

	const auto manual_priority = 1.f;  // TODO [0, 1] range, default = 1
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

static glm::mat4 light_view_projection(const GPULight &light, size_t idx)
{
	if(IS_POINT_LIGHT(light))
	{
		static constexpr auto aspect = 1.f;  // i.e. square

		const auto &view_forward = s_cube_face_forward[idx];
		const auto &view_up      = s_cube_face_up[idx];

		const auto light_view      = glm::lookAt(light.position, light.position + view_forward, view_up);
		const auto face_projection = glm::perspective(glm::radians(90.0f), aspect, 0.05f, light.affect_radius);
		const auto light_vp        = face_projection * light_view;

		return light_vp;
	}
	if(IS_DIR_LIGHT(light))
	{
		// TODO
		//   3 cascades (CSM)
	}
	if(IS_SPOT_LIGHT(light))
	{
		static constexpr auto aspect = 1.f;  // i.e. square

		const auto view_forward = light.direction;
		auto view_up = AXIS_Z;

		const auto light_view = glm::lookAt(light.position, light.position + view_forward, view_up);
		const auto projection = glm::perspective(glm::radians(light.outer_angle), aspect, 0.05f, light.affect_radius);
		const auto light_vp   = projection * light_view;

		return light_vp;
	}

	return glm::mat4(1);
}
