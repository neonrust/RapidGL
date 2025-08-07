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
	using SlotSize = decltype(_allocator)::SizeT;
	using SlotID = decltype(_allocator)::NodeIndex;

	//dense_map<LightID, Slot> _lights;
	struct SlotDef
	{
		SlotSize size;
		SlotID node_index;
		glm::uvec4 rect;       // TODO: only store 'top_left'? rect.zw is same as 'size'
	};
	using LightSlots = std::array<SlotDef, 6>;
	struct AtlasLight
	{
		LightID uuid;
		size_t num_slots;     // point: 6, dir: 3?, all others: 1
		LightSlots slots;     // per slot:

		inline bool is_dirty() const { return _dirty; }
		inline void on_rendered(Time t) const
		{
			_dirty = false;
			last_rendered = t;
		}

	private:
		mutable bool _dirty;
		float prev_light_value;
		mutable Time last_rendered;   // TODO: define a "pixels per second" limit for how many shadow map slots can be updated (in light-value/age order?)
		Time last_size_change;

		friend class ShadowAtlas;
	};
	static_assert(sizeof(AtlasLight) == 184);

public:

	ShadowAtlas(uint32_t size); // TODO: specify which channels to use (e.g. depth & normals)
	~ShadowAtlas();

	bool create();

	inline void set_max_casters(size_t max_casters) { _max_shadow_slots = max_casters; }
	inline void set_max_distance(float max_distance) { _max_distance = max_distance; _large_light_radius = _max_distance; }
	inline void set_min_change_interval(std::chrono::milliseconds interval) { _min_change_interval = interval; }

	size_t eval_lights(LightManager &lights, const glm::vec3 &view_pos, const glm::vec3 &view_forward);

	[[nodiscard]] const dense_map<LightID, AtlasLight> &allocated() const { return _id_to_allocated; }

	void set_rendered(LightID uuid, std::chrono::steady_clock::time_point t=std::chrono::steady_clock::now());

private:
	float light_value(const GPULight &light, const glm::vec3 &view_pos, const glm::vec3 &view_forward) const;
	struct ApplyCounters
	{
		size_t allocated;
		size_t retained;
		size_t promoted;
		size_t demoted;
		size_t change_pending;
	};
	ApplyCounters apply_desired_slots(LightManager &lights, const small_vec<AtlasLight, 120> &desired_slots, Time now);
	void generate_slots(std::initializer_list<uint32_t> distribution);
	bool slots_available(const AtlasLight &atlas_light) const;
	bool remove_allocation(LightID light_id);
	SlotID alloc_slot(SlotSize size);
	void free_slot(SlotSize size, SlotID node_index);

	void _dump_desired(const small_vec<AtlasLight, 120> &desired_slots);

private:
	struct ValueLight
	{
		float value;
		LightID light_id;
		size_t num_slots;
	};
	dense_map<SlotSize, std::vector<SlotID>> _slot_sets;

	dense_map<LightID, AtlasLight> _id_to_allocated;
	AtlasLight _allocated_sun;

	size_t _max_shadow_slots;
	float _max_distance { 50.f };
	float _large_light_radius { 50.f };

	// shortest interval an allocated slot can change size (toggle)
	std::chrono::milliseconds _min_change_interval;

	RGL::buffer::ShaderStorage<LightShadowParams> _shadow_params_ssbo;
	small_vec<size_t, 16> _distribution;  // slot sizes of each of the levels (from max to min)
};
