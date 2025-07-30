#include "shadow_atlas.h"

#include <glm/vec3.hpp>

#include "light_constants.h"
#include "light_manager.h"

#include <chrono>
#include <ranges>
#include <algorithm>
#include <string_view>
#include <print>

#include "buffer_binds.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "constants.h"

using namespace std::literals;

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

static const std::string_view _light_type_names[] {
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
	_change_min_interval(3s),
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

void ShadowAtlas::set_rendered(LightID uuid, steady_clock::time_point t)
{
	auto found = _id_to_allocated.find(uuid);
	if(found == _id_to_allocated.end())
		return;

	found->second.on_rendered(t);
}


const dense_map<LightID, ShadowAtlas::AtlasLight> &ShadowAtlas::eval_lights(LightManager &lights, const glm::vec3 &view_pos)
{
	const Time T0 = steady_clock::now();

	static std::vector<ValueLight> prioritized;
	prioritized.reserve(lights.size());
	prioritized.clear();

	float strongest_dir_value { -1.f };

	// calculate "value" for each shadow-casting light
	LightIndex light_index = 0;
	for(const auto &light: lights)
	{
		if(IS_SHADOW_CASTER(light))
		{
			auto value = light_value(light, view_pos);
			if(value > 0)
			{
				if(IS_DIR_LIGHT(light) and value > strongest_dir_value)
				{
					_allocated_sun.uuid = lights.light_id(light_index);
					strongest_dir_value = value;
				}
				else
				{
					prioritized.push_back({
						.value = value,
						.light_id = lights.light_id(light_index),
						.num_slots = LIGHT_NUM_SLOTS(light),
					});
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
			.num_slots = 1,
		});
	}

	std::ranges::sort(prioritized, [](const auto &A, const auto &B) {
		return A.value > B.value;
	});
	if(_allocated_sun.uuid != NO_LIGHT_ID)
		assert(IS_DIR_LIGHT(lights.get_by_id(prioritized[0].light_id).value()));

	// count how many lights we have slots for
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
			lights.clear_shadow_index(prio.light_id);

			// free the previous slot (if any)
			if(auto found = _id_to_allocated.find(prio.light_id); found != _id_to_allocated.end())
			{
				const auto &atlas_light = found->second;
				for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
					free_slot(atlas_light.slots[idx].size, atlas_light.slots[idx].node_index);

				_id_to_allocated.erase(found);
			}
		}

		// cut off excess lights
		prioritized.resize(space_for_lights);
	}

	// "pour" the valued lights into the size-buckets.
	//   the top light's value determines which size to start at.
	//   the rest follows in value order, with decreasing sizes as distribution allows.

	small_vec<AtlasLight, 120> desired_slots;

	const auto top_light_value = prioritized.begin()->value;
	// value = 1 is the most important possible; gets the highest-resolution slot
	small_vec<size_t, 8> distribution(_distribution.begin(), _distribution.end());
	auto distribution_iter = distribution.begin();
	const auto start_size_idx = static_cast<uint32_t>(std::ceil(static_cast<float>(distribution.size()) * (1 - top_light_value)));
	std::advance(distribution_iter, start_size_idx);

	auto start_size = _allocator.max_size() >> start_size_idx;

	// TODO: directional lights, using CSM. see: https://learnopengl.com/Guest-Articles/2021/CSM
	//   might be reasonable to dedicate 3 slots to it; 1024, 512, 256, straight up

	// "drop" the light at the top of 'distribution' (start size)
	// allotting where there's first space.

	for(const auto &prio_light: prioritized)
	{
		const auto &light_ = lights.get_by_id(prio_light.light_id);
		assert(light_.has_value());
		const auto &light = light_.value();

		AtlasLight atlas_light;
		atlas_light.uuid      = prio_light.light_id;
		atlas_light.num_slots = LIGHT_NUM_SLOTS(light);
		// atlas_light.rect        // from allocation in shadow map, below
		// atlas_light.map_node    // from allocation in shadow map, below

		auto iter = distribution_iter;
		auto slot_size = start_size;
		while(*iter < atlas_light.num_slots and iter != distribution.end())
		{
			++iter;
			slot_size >>= 1;
		}
		if(iter != distribution.end())
		{
			// consume the required slots
			atlas_light.slots[0].size = slot_size;
			desired_slots.push_back(atlas_light);
			*iter -= atlas_light.num_slots;

			// skip any ampty items
			while(*distribution_iter == 0 and distribution_iter != distribution.end())
			{
				++distribution_iter;
				start_size >>= 1;
			}
			if(distribution_iter == distribution.end())
				break;
		}
	}

	// _dump_desired(desired_slots);

	apply_desired_slots(lights, desired_slots, T0);

	std::print("Allotted shadow map space for {} of {} lights, in {}\n",
			   desired_slots.size(), prioritized.size(), duration_cast<microseconds>(steady_clock::now() - T0));

	return _id_to_allocated;
}

void ShadowAtlas::_dump_desired(const small_vec<ShadowAtlas::AtlasLight, 120> &desired_slots)
{
	std::print("--- Desired slots ({}):\n", desired_slots.size());
	for(const auto &desired: desired_slots)
	{
		std::print("  [{}] size: {}  x{}\n", desired.uuid, desired.slots[0].size, desired.num_slots);
	}
}


static glm::mat4 light_view_projection(const GPULight &light, size_t idx=0);

void ShadowAtlas::apply_desired_slots(LightManager &lights, const small_vec<AtlasLight, 120> &desired_slots, const Time now)
{
	small_vec<std::tuple<bool, LightID>, 120> size_changed; // { new_slot, id }
	small_vec<decltype(_shadow_params_ssbo)::value_type, 120> shadow_params;

	auto add_params = [&shadow_params, &lights](LightID light_id, const std::array<glm::uvec4, 6> &rects) {
		auto light_ = lights.get_by_id(light_id);
		auto &light = light_.value();

		std::array<glm::mat4, 6> projs;
		projs[0] = light_view_projection(light);
		if(IS_POINT_LIGHT(light))
		{
			for(auto idx = 1u; idx < 6; ++idx)
				projs[idx] = light_view_projection(light, idx);
		}

		shadow_params.push_back(LightShadowParams {
			.view_proj = projs,
			.atlas_rect = rects,
		});

		lights.set_shadow_index(light_id, uint_fast16_t(shadow_params.size() - 1));
	};

	auto num_unchanged = 0u;
	auto num_allocated = 0u;
	auto num_Pending_change = 0;
	auto num_promoted = 0u;
	auto num_demoted = 0;

	// compare desired slot sizes to currently allocated slots
	for(const auto &desired: desired_slots)
	{
		const auto light_id = desired.uuid;

		std::array<glm::uvec4, 6> rects;

		auto found = _id_to_allocated.find(light_id);
		if(found == _id_to_allocated.end())
		{
			// NEW shadow slot
			auto light_ = lights.get_by_id(light_id);
			auto &light = light_.value();
			const auto num_slots = IS_POINT_LIGHT(light)? 6u: (IS_DIR_LIGHT(light)? 3: 1u);
			auto atlas_light = desired;
			auto &free_slots = _slot_sets[atlas_light.slots[0].size];
			assert(free_slots.capacity() > 0);
			auto idx = 0u;
			for(; idx < num_slots; ++idx)
			{
				const auto node_index = alloc_slot(atlas_light.slots[0].size);

				atlas_light.slots[idx].node_index = node_index;
				const auto rect = _allocator.rect(node_index);
				rects[idx] = glm::uvec4(rect.x, rect.y, rect.w, rect.h);
			}
			_id_to_allocated[light_id] = atlas_light;
			size_changed.push_back({ true, light_id });
			rects[idx] = atlas_light.slots[0].rect;
			++num_allocated;
		}
		else
		{
			// was allocated before, compare it
			auto &atlas_light = found->second;
			atlas_light.last_used = now;

			const auto size_diff = desired.slots[0].size - atlas_light.slots[0].size;
			const auto change_age = now - atlas_light.last_size_change;

			if(size_diff == 0 or change_age < _change_min_interval)
			{
				// std::print("[{}] size kept {}", light_id, atlas_light.slots[0].size);
				if(size_diff != 0)
					++num_Pending_change;

				// no change to _id_to_allocated
				for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
					rects[idx] = atlas_light.slots[idx].rect;
				++num_unchanged;
			}
			else
			{
				if(size_diff > 0)
					++num_promoted;
				else
					++num_demoted;

				auto light_ = lights.get_by_id(light_id);
				auto &light = light_.value();
				const auto num_slots = IS_POINT_LIGHT(light)? 6u: 1u;


				for(auto idx = 0u; idx < num_slots; ++idx)
				{
					// return the previous size slot to the pool
					free_slot(atlas_light.slots[idx].size, atlas_light.slots[idx].node_index);

					atlas_light.slots[idx].size = desired.slots[idx].size;

					// get the new slots from the desired slots size
					auto node_index = alloc_slot(desired.slots[idx].size);
					atlas_light.slots[idx].node_index= node_index;

					atlas_light.slots[idx].rect = to_uvec4(_allocator.rect(node_index));
					rects[idx] = atlas_light.slots[idx].rect;
				}

				atlas_light.last_size_change = now;
				// size_changed.push_back({ false,  light_id });

				_id_to_allocated[light_id] = atlas_light;
			}
		}

		add_params(light_id, rects);
	}

	std::print("new:{} kept:{} prmote:{} demote:{}  (pending: {})\n",
			   num_allocated, num_unchanged, num_promoted, num_demoted, num_Pending_change);

	_shadow_params_ssbo.set(shadow_params);
}

void ShadowAtlas::free_slot(SlotSize size, SlotID node_index)
{
	assert(_slot_sets.contains(size));

	_slot_sets[size].push_back(node_index);
}

ShadowAtlas::SlotID ShadowAtlas::alloc_slot(SlotSize size)
{
	assert(_slot_sets.contains(size));

	auto &free_slots = _slot_sets[size];
	assert(not free_slots.empty());

	auto node_index = free_slots.back();
	free_slots.pop_back();

	return node_index;
}

float ShadowAtlas::light_value(const GPULight &light, const glm::vec3 &view_pos) const
{
	// calculate the "value" of a light on a fixed scale  [0, 1]

	assert(_max_distance > 0);

	if(IS_DIR_LIGHT(light))  // i.e. the sun, which is always relevant
		return 1.f;

	// distance from camera to edge of the light's radius sphere
	auto distance = glm::distance(light.position, view_pos);
	distance = glm::max(0.f, distance - light.affect_radius);

	if(distance >= _max_distance) // too far away
		return 0.f;
	if(distance == 0.f)
		return 1.f;

	// distance = 0 means view_pos is inside the light's sphere
	// scale as a fraction of the max distance
	auto value = glm::clamp(1.f - distance / _max_distance, 0.f, 1.f);

	// TODO: scale down small-radius lights ?
	//   maybe based on size of the far plane in world units?
	//   essentially, pick a "pixels per meter" and scale based on that.

	// non-linearize it "a bit"  TODO: pick good exponent :)
	value = 1 - std::pow(1 - value, 2.f);

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
			free_slots.push_back(index);
		}
		_max_shadow_slots += free_slots.size();

		size >>= 1;
	}

	// then the remaining space is allocated using the smallest size
	auto &free_slots = _slot_sets[size];
	free_slots.reserve(_distribution[_distribution.size() - 2] << 1); // at least twice as many as the previous size
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

	// TODO make ascii art of allocated slots, somehow?
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
		//   cascades
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
