#include "shadow_atlas.h"

#include <glm/vec3.hpp>

#include "light_constants.h"
#include "light_manager.h"


using namespace std::chrono;

ShadowAtlas::ShadowAtlas(uint32_t size) :
	RGL::RenderTarget::Texture2d(),
	_allocator(size, 64)
{
	assert(size >= 1024 and size <= 16384 and __builtin_popcount(size) == 1);


	_distribution = {
		{ size >> 3, 12 },  // e.g. space for 2 point lights or 1 point and 6 spots (1024 of 8192)
		{ size >> 4, 24 },  // 512 of 8192
		{ size >> 5, 32 },  // 256 of 8192
		{ size >> 6, 64 },  // 128 of 8192
		{ size >> 7, 0 },   // 0 = the rest  TODO: calculate how many fit in the atlas?
	};

	// TODO: use the allocator to calculate how many slots are possible
	//  e.g. allocate the first four levels, than ask the allocator to count how many of the last slot there is space for

	_max_shadow_casters = std::accumulate(_distribution.begin(), _distribution.end(), 0u, [](size_t sum, const auto &item) {
		return item.num_slots + sum;
	});
}

ShadowAtlas::~ShadowAtlas()
{
	release();
}

bool ShadowAtlas::setup()
{
	const auto size = _allocator.size();

	namespace C = RGL::RenderTarget::Color;
	namespace D = RGL::RenderTarget::Depth;
	RGL::RenderTarget::Texture2d::create("shadow-atlas", size, size, C::Texture | C::Float2, D::Texture | D::Float);

	return bool(this);
}

const std::vector<ShadowAtlas::AtlasLight> &ShadowAtlas::eval_lights(LightManager &lights, const glm::vec3 &view_pos)
{
	if(_allocated_slots.size() == 0)
		_allocated_slots.reserve(_max_shadow_casters);
	decltype(_allocated_slots) candidates;
	candidates.reserve(_max_shadow_casters);

	static std::vector<ValueLight> prioritized;
	// prioritized.reserve(counts.num_point_lights + counts.num_spot_lights + counts.num_area_lights);
	prioritized.reserve(256);
	prioritized.clear();

	static dense_map<LightID, GPULight *> lightRef;
	// lightRef.reserve(counts.num_point_lights + counts.num_spot_lights + counts.num_area_lights);
	lightRef.clear();


	// iterate over all (shadow-casting) lights
	LightManager::Index light_index = 0;
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
				});
			}
		}

		++light_index;
	}

	std::sort(prioritized.begin(), prioritized.end(), [](const auto &A, const auto &B) {
		return A.value > B.value;
	});

	if(prioritized.size() > _max_shadow_casters)
	{
		for(auto iter = prioritized.begin() + ptrdiff_t(_max_shadow_casters); iter != prioritized.end(); ++iter)
		{
			lights.clear_shadow_index(iter->light_id);
			// TODO: remove the light's allocation from the atlas (if any)
		}

		// cut off excess lights
		prioritized.resize(_max_shadow_casters);
	}

	auto prio_iter = prioritized.begin();

	std::vector<small_vec<decltype(prioritized)::value_type, 64>> buckets;

	// TODO: "fill" the valued lights into the size-buckets
	//   the top light's value determines which size to start at
	//   the rest follows in value-order, with decreasing sizes.

	auto start_size_idx = uint32_t(std::ceil(float(_distribution.size()) * prio_iter->value));

	dense_map<LightID, uint32_t> desired_slots;


	auto remaining_slots = _distribution;
	for(; prio_iter != prioritized.end(); ++prio_iter)
	{
		for(auto idx = start_size_idx; idx < _distribution.size(); ++idx)
		{
			auto &num_slots = remaining_slots[idx].num_slots;
			if(num_slots == 0)
				continue;
			--num_slots;
		}
	}

	// map uuid -> slot size

	// TODO: compare size-buckets from currently allocated slots (_id_to_allocated)


	return _allocated_slots;
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
