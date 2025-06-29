#pragma once

#include "rendertarget_2d.h"
#include "container_types.h"
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "spatial_allocator.h"
#include "lights.h"

class LightManager;

namespace RGL {
class Camera;
}
struct GPULight;

using Time = std::chrono::steady_clock::time_point;

class ShadowAtlas : public RGL::RenderTarget::Texture2d
{
public:
	static constexpr size_t PADDING = 1;
	enum class CubeFace : uint32_t { PosX, NegX, PosY, NegY, PosZ, NegZ };

private:
	RGL::SpatialAllocator<uint32_t> _allocator;

public:
	using LightIndex = uint32_t;//LightManager::Index;
	using AllocatedIndex = decltype(_allocator)::NodeIndex;
	using SlotSize = decltype(_allocator)::SizeT;

	//dense_map<LightID, Slot> _lights;
	struct AtlasLight
	{
		LightID uuid;
		SlotSize slot_size;        // 0 if no slot assigned?
		size_t num_clots;  // point: 6, all others: 1
		std::array<AllocatedIndex, 6> map_node;
		std::array<glm::uvec4, 6> rect;

	private:
		SlotSize prev_slot_size;
		float light_value;
		float prev_light_value;
		Time last_used;
		Time last_updated;
		Time last_size_change;
	};

public:
	ShadowAtlas(uint32_t size);
	~ShadowAtlas();

	bool setup();

	inline void set_max_distance(float max_distance) { _max_distance = max_distance; }

	//const Slot &point_light(LightID uuid, float importance=1);

	const std::vector<AtlasLight> &eval_lights(LightManager &lights, const glm::vec3 &view_pos);

private:
	float light_value(const GPULight &light, const glm::vec3 &view_pos) const;

private:
	struct SizeSlots
	{
		SlotSize size;
		size_t num_slots;
	};
	struct ValueLight
	{
		float value;
		LightID light_id;
	};
	struct SizeBucket
	{
		SlotSize size;
		small_vec<LightID, 64> lights;
	};

	small_vec<SizeSlots> _distribution;
	//small_vec<ValueLight, 127> _value_lights;
	//dense_map<float, LightID> _prioritized;
	dense_map<LightID, AtlasLight> _id_to_allocated;
	size_t _max_shadow_casters;

	float _max_distance { 50.f };

	std::vector<AtlasLight> _allocated_slots;
};
