#include "shadow_atlas.h"
#include <glm/integer.hpp>

#include "light_constants.h"


namespace RGL
{

using namespace std::chrono;

ShadowAtlas::ShadowAtlas(uint32_t size) :
	RenderTarget::Texture2d(),
	_allocator(size)
{
	assert(size >= 1024 and size <= 16384 and glm::bitCount(size) == 1);


	_distribution = {
		{ size >> 3, 12 },  // e.g. space for 2 point lights (1024 of 8192)
		{ size >> 4, 24 },  // 512 of 8192
		{ size >> 5, 32 },  // 256 of 8192
		{ size >> 6, 64 },  // 128 of 8192
		{ size >> 7, std::numeric_limits<size_t>::max() }, // 0 = the rest
	};
	_max_shadow_casters = std::accumulate(_distribution.begin(), _distribution.end(), 0u, [](size_t sum, const auto &item) {
		return std::get<1>(item) + sum;
	});
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
	Texture2d::create("shadow-atlas", size, size, C::Texture | C::Float2, D::Texture | D::Float);

	return bool(this);
}

void ShadowAtlas::eval_lights(const LightManager &lights)
{
	if(_allocated.size() == 0)
		_allocated.reserve(_max_shadow_casters);
	decltype(_allocated) candidates;
	candidates.reserve(_max_shadow_casters);

	static std::vector<std::tuple<float, LightManager::Index>> prioritized;
	// prioritized.reserve(counts.num_point_lights + counts.num_spot_lights + counts.num_area_lights);
	prioritized.clear();

	static dense_map<LightID, GPULight *> lightRef;
	// lightRef.reserve(counts.num_point_lights + counts.num_spot_lights + counts.num_area_lights);
	lightRef.clear();


	// TODO: iterate over all (shadow-casting) lights
	LightManager::Index light_index = 0;
	for(const auto &light: lights)
	{
		if((light.type_flags & LIGHT_SHADOW_CASTER) > 0)
			prioritized.push_back({ _importance(light), light_index });

		++light_index;
	}

	std::sort(prioritized.begin(), prioritized.end());
/*
	// TODO: create uuid -> light data mapping
	for(const auto &[size, count]: _distribution)
	{
		Bucket *bucket = nullptr;
		auto found = _buckets.find(size);
		if(found)
			bucket = &found.first;;
		else
			bucket = &_buckets[size];
		if(bucket->count() == count)
			;
	}
*/
	// TODO: catregorize into size "buckets"
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

} // RGL
