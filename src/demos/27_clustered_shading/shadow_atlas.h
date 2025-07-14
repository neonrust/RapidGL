#pragma once

#include "rendertarget_2d.h"
#include "container_types.h"
#include "spatial_allocator.h"
#include "lights.h"
#include "ssbo.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "generated/shared-structs.h"

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
	using AllocatedIndex = decltype(_allocator)::NodeIndex;
	using SlotSize = decltype(_allocator)::SizeT;

	//dense_map<LightID, Slot> _lights;
	struct AtlasLight
	{
		LightID uuid;
		SlotSize slot_size;
		size_t num_slots;              // point: 6, all others: 1
		std::array<AllocatedIndex, 6> map_node;
		std::array<glm::uvec4, 6> rect;

		inline bool is_dirty() const { return _dirty; }
		inline void on_updated(Time t) const
		{
			_dirty = false;
			last_used = t;
			last_updated = t;
		}

	private:
		mutable bool _dirty;
		mutable Time last_used;
		mutable Time last_updated;
		SlotSize prev_slot_size;
		float light_value;
		float prev_light_value;
		Time last_size_change;

		friend class ShadowAtlas;
	};

public:

	ShadowAtlas(uint32_t size); // TODO: specify which channels to use (e.g. depth & normals)
	~ShadowAtlas();

	bool create();

	inline void set_max_casters(size_t max_casters) { _max_shadow_casters = max_casters; }
	inline void set_max_distance(float max_distance) { _max_distance = max_distance; }
	inline void set_min_change_interval(std::chrono::milliseconds interval) { _change_min_interval = interval; }

	//const Slot &point_light(LightID uuid, float importance=1);


	const dense_map<LightID, AtlasLight> &eval_lights(LightManager &lights, const glm::vec3 &view_pos);

private:
	float light_value(const GPULight &light, const glm::vec3 &view_pos) const;
	void apply_desired_slots(LightManager &lights, const small_vec<AtlasLight, 120> &desired_slots, Time now);
	void generate_slots(std::initializer_list<uint32_t> distribution);

private:
	struct ValueLight
	{
		float value;
		LightID light_id;
		LightIndex light_index;
	};
	struct AvailableSlots
	{
		bool in_use;
		decltype(_allocator)::NodeIndex node;
	};
	dense_map<SlotSize, small_vec<AvailableSlots, 120>> _available_slots;

	dense_map<LightID, AtlasLight> _id_to_allocated;

	size_t _max_shadow_casters { 64 };
	float _max_distance { 50.f };

	// shortest interval an allocated slot can change size (toggle)
	std::chrono::milliseconds _change_min_interval;

	RGL::buffer::ShaderStorage<LightShadowParams> _shadow_params_ssbo;
	small_vec<size_t, 16> _distribution;  // slot sizes of each of the levels (from max to min)
};
