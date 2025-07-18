#include "shadow_atlas.h"

#include <glm/vec3.hpp>

#include "light_constants.h"
#include "light_manager.h"

#include <chrono>
#include <ranges>
#include <algorithm>

#include "buffer_binds.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "constants.h"
#include "glm/ext/vector_integer.hpp"

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

using namespace std::chrono;
using namespace std::literals;

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
	_shadow_params_ssbo("shadow-params"),
	_allocator(size, size >> 6, size >> 3),
	_change_min_interval(3s)
{
	if(__builtin_popcount(size) != 1)
		size = 1 << (sizeof(size)*8 -  __builtin_clz(size));  // round up to next Po2
	assert(size >= 1024 and size <= 16384);

	_shadow_params_ssbo.setBindIndex(SSBO_BIND_SHADOW_PARAMS);

	generate_slots({ 24, 64, 256 });

	_max_shadow_casters = std::accumulate(_available_slots.begin(), _available_slots.end(), 0u, [](auto sum, const auto &item) {
		return item.second.size() + sum;
	});

	std::print("ShadowAtlas: total shadow casters: {}\n", _max_shadow_casters);
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

const dense_map<LightID, ShadowAtlas::AtlasLight> &ShadowAtlas::eval_lights(LightManager &lights, const glm::vec3 &view_pos)
{
	const Time now = steady_clock::now();

	static std::vector<ValueLight> prioritized;
	prioritized.reserve(lights.size());
	prioritized.clear();

	// calculate "value" for each shadow-casting light
	LightIndex light_index = 0;
	for(const auto &light: lights)
	{
		if(IS_SHADOW_CASTER(light))
		{
			auto value = light_value(light, view_pos);
			if(value > 0)
			{
				prioritized.push_back({
					.value = value,
					.light_id = lights.light_id(light_index),
					.light_index = light_index,
				});
			}
		}

		++light_index;
	}

	std::ranges::sort(prioritized, [](const auto &A, const auto &B) {
		return A.value > B.value;
	});

	if(prioritized.size() > _max_shadow_casters)
	{
		std::print("Shadow casters capped {} -> {}\n", prioritized.size(), _max_shadow_casters);

		for(auto iter = prioritized.begin() + ptrdiff_t(_max_shadow_casters); iter != prioritized.end(); ++iter)
		{
			lights.clear_shadow_index(iter->light_id);
			// remove the light's allocation from the atlas (if any)
			if(_id_to_allocated.erase(iter->light_id))
				std::print("[{}] deallocated\n", iter->light_id);
		}

		// cut off excess lights
		prioritized.resize(_max_shadow_casters);
	}

	// "pour" the valued lights into the size-buckets.
	//   the top light's value determines which size to start at.
	//   the rest follows in value order, with decreasing sizes as distribution allows.

	small_vec<AtlasLight, 120> desired_slots;

	const auto top_light_value = prioritized.begin()->value;
	// value = 1 is the most important possible; gets the highest-resolution slot
	auto distribution_iter = _distribution.begin();
	const auto start_size_idx = static_cast<uint32_t>(std::ceil(static_cast<float>(_distribution.size()) * (1 - top_light_value)));
	std::advance(distribution_iter, start_size_idx);

	auto alloted = 0u;

	auto slot_size = _allocator.max_size() >> start_size_idx;
	auto num_slots = *distribution_iter;

	for(const auto &light_index: prioritized)
	{
		assert(num_slots > 0);
		const auto &light_ = lights.get_by_index(light_index.light_index);
		assert(light_.has_value());
		const auto &[light_id, light] = light_.value();

		AtlasLight atlas_light;
		atlas_light.uuid      = light_id;
		atlas_light.slot_size = slot_size;
		atlas_light.num_slots = IS_POINT_LIGHT(light)? 6: 1;
		// atlas_light.rect        // from allocation in shadow map, below
		// atlas_light.map_node    // from allocation in shadow map, below

		desired_slots.push_back(atlas_light);

		++alloted;
		if(--num_slots == 0)
		{
			slot_size >>= 1;
			if(++distribution_iter == _distribution.end())
				break;  // no more slots
			num_slots = *distribution_iter;
		}
	}

	std::print("Alloted shadow map space for {} of {} lights\n", alloted, prioritized.size());

	apply_desired_slots(lights, desired_slots, now);

	return _id_to_allocated;
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

		lights.set_shadow_index(light_id, shadow_params.size() - 1);
	};

	// compare desired slot sizes to currently allocated slots
	for(const auto &desired: desired_slots)
	{
		const auto light_id = desired.uuid;

		std::array<glm::uvec4, 6> rects;

		auto found = _id_to_allocated.find(light_id);
		if(found == _id_to_allocated.end())
		{
			// NEW shadow slot
			std::print("[{}] allocated {}\n", light_id, desired.slot_size);
			auto light_ = lights.get_by_id(light_id);
			auto &light = light_.value();
			const auto num_slots = IS_POINT_LIGHT(light)? 6u: 1u;
			auto atlas_light = desired;
			for(auto idx = 0u; idx < num_slots; ++idx)
			{
				auto node = _allocator.allocate(desired.slot_size);
				atlas_light.map_node[idx] = node;
				const auto rect = _allocator.rect(node);
				rects[idx] = glm::uvec4(rect.x, rect.y, rect.w, rect.h);
			}
			_id_to_allocated[light_id] = atlas_light;
			size_changed.push_back({ true,  light_id });
			rects = atlas_light.rect;
		}
		else
		{
			// was allocated before, compare it
			auto &atlas_light = found->second;
			atlas_light.last_used = now;

			const auto size_diff = desired.slot_size - atlas_light.slot_size;
			const auto change_age = now - atlas_light.last_size_change;

			if(size_diff == 0 or change_age < _change_min_interval)
			{
				std::print("[{}] size kept {}", light_id, atlas_light.slot_size);
				if(size_diff == 0)
					std::puts("");
				else
					std::print(", but {} pending ({})\n", desired.slot_size, change_age);
				// no change to _id_to_allocated
				rects = atlas_light.rect;
			}
			else
			{
				if(size_diff > 0)
					std::print("[{}] promoted {} -> {} (after {})\n", light_id, atlas_light.slot_size, desired.slot_size, change_age);
				else
					std::print("[{}] demoted {} -> {} (after {})\n", light_id, atlas_light.slot_size, desired.slot_size, change_age);
				atlas_light.slot_size = desired.slot_size;
				atlas_light.last_size_change = now;
				size_changed.push_back({ false,  light_id });

				auto light_ = lights.get_by_id(light_id);
				auto &light = light_.value();
				const auto num_slots = IS_POINT_LIGHT(light)? 6u: 1u;
				for(auto idx = 0u; idx < num_slots; ++idx)
				{
					auto node = _allocator.allocate(desired.slot_size);
					atlas_light.map_node[idx] = node;

					const auto rect = _allocator.rect(node);
					atlas_light.rect[idx] = glm::uvec4(rect.x, rect.y, rect.w, rect.h);
				}
				_id_to_allocated[light_id] = atlas_light;
				rects = atlas_light.rect;
			}
		}

		add_params(light_id, rects);
	}

	// updated entries first, then new entries
	std::ranges::sort(size_changed);

	// (re-)allocate slot sizes (in size_changed)
	for(const auto &[new_slot, light_id]: size_changed)
	{
		auto &atlas_light = _id_to_allocated[light_id];

		if(not new_slot)
		{
			// size change, de-allocate the old slot
			for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
				_allocator.free(atlas_light.map_node[idx]);
		}

		// allocate new entry in shadow map
		for(auto idx = 0u; idx < atlas_light.num_slots; ++idx)
		{
			auto index = _allocator.allocate(atlas_light.slot_size);
			assert(index != _allocator.end());
			atlas_light.map_node[idx] = index;
		}
	}

	_shadow_params_ssbo.set(shadow_params);
}

float ShadowAtlas::light_value(const GPULight &light, const glm::vec3 &view_pos) const
{
	assert(_max_distance > 0);

	// calculate the "value" of a light on a fixed scale

	// distance from camera to edge of the light's radius sphere
	//  as fraction of the max distance

	if(IS_DIR_LIGHT(light))  // i.e. the sun
		return 1.f;

	// TODO: scale down small-radius lights... somehow
	//   maybe based on far-plane size is world units?
	//   pick a "pixels per meter" and scale based on that.

	float distance = glm::distance(light.position, view_pos) - light.affect_radius;

	auto value = glm::clamp(1.f - distance / _max_distance, 0.f, 1.f);

	// non-linearize it "a bit"
	value = 1 - std::pow(1 - value, 0.5f); // TODO: pick good exponent :)

	return value;
}

void ShadowAtlas::generate_slots(std::initializer_list<uint32_t> distribution)
{
	const auto T0 = steady_clock::now();

	_distribution.assign(distribution.begin(), distribution.end());   // max_size -> min_size (last is calculated below)

	_available_slots.reserve(_distribution.size() + 1); // including the left-over smallest size
	// TODO: size of '_available_slots' should match the levels possible to allocate in '_allocator'

	// use the allocator to calculate how many slots are possible
	//  e.g. allocate the first four levels of the distribution,
	//  than ask the allocator to count how many of the last size there is room for
	auto size = _allocator.max_size();

	for(const auto count: _distribution)
	{
		auto &avail_slots = _available_slots[size];
		avail_slots.reserve(count);

		for(auto idx = 0u; idx < count; ++idx)
		{
			const auto index = _allocator.allocate(size);
			assert(index != _allocator.end());
			avail_slots.push_back({ false, index });
		}

		size >>= 1;
	}

	// then the remaining space is allocated as the smallest size
	const auto small_size = size;
	auto &avail_slots = _available_slots[small_size];
	avail_slots.reserve(_distribution[_distribution.size() - 2] << 1); // at least twice as many as the previous size
	do
	{
		const auto index = _allocator.allocate(small_size);
		if(index != _allocator.end())
			avail_slots.push_back({ true, index });
		else
			break;
	}
	while(true);

	_distribution.push_back(avail_slots.size());

	const auto Td = steady_clock::now() - T0;

	std::print("ShadowAltas: {} slots of size {}, in {} µs\n",
		avail_slots.size(), small_size, duration_cast<microseconds>(Td));
}

/*
const ShadowAtlas::Slot &ShadowAtlas::point_light(LightID uuid, float importance)
{
	auto existing = _lights.find(uuid);
	if(existing != _lights.end())
	{
		auto &[_, slot] = *existing;
		slot.importance = importance;
		slot.last_used = steady_clock::now();
		return slot;
	}
	else
	{
		// _allocator.allocate
	}
}
*/

static glm::mat4 light_view_projection(const GPULight &light, size_t idx)
{
	if(IS_POINT_LIGHT(light))
	{
		static constexpr auto aspect = 1.f;  // i.e. square

		const auto &view_forward = s_cube_face_forward[idx];
		const auto &view_up      = s_cube_face_up[idx];

		const auto light_view      = glm::lookAt(light.position, light.position + view_forward, view_up);
		const auto face_projection = glm::perspective(glm::radians(90.0f), aspect, 0.1f, light.affect_radius);
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
		// TODO
	}

	return glm::mat4(1);
}
