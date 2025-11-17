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

struct GPULight;

using Time = std::chrono::steady_clock::time_point;

class ShadowAtlas : public RGL::RenderTarget::Texture2d
{
public:
	static constexpr size_t PADDING = 1;
	enum class CubeFace : uint32_t { PosX, NegX, PosY, NegY, PosZ, NegZ };

private:
	using allocator_t = RGL::SpatialAllocator<uint32_t>;

public:
	using SlotSize = allocator_t::SizeT;
	using SlotID = allocator_t::NodeIndex;

	struct SlotDef
	{
		SlotSize size;
		SlotID node_index;
		glm::uvec4 rect;
	};
	using LightSlots = std::array<SlotDef, 6>;
	struct AtlasLight
	{
		inline AtlasLight() : _dirty(true) {}
		AtlasLight(const AtlasLight &other);
		AtlasLight &operator = (const AtlasLight &other) = default;

		LightID uuid;
		size_t num_slots;     // point: 6, dir: 3?, all others: 1; size of 'slots' array
		LightSlots slots;     // per slot

		inline bool is_dirty() const { return _dirty; }

		inline void set_dirty() const { _dirty = true; }       // called from allocated_lights(); const
		inline void on_rendered(Time t, size_t new_hash) const // called from allocated_lights(); const
		{
			_dirty = false;
			_last_rendered = t;
			hash = new_hash;
			_frames_skipped = 0;
		}

		mutable size_t hash;

	private:
		mutable bool _dirty;
		mutable Time _last_rendered;   // TODO: define a "pixels per second" limit to dictate a limit for how many shadow map slots may be updated (in light-value/age order?)
		mutable uint32_t _frames_skipped;
		Time _last_size_change;

		friend class ShadowAtlas;
	};
	static_assert(sizeof(AtlasLight) == 200);

public:

	// TODO: specify which channels to use (e.g. depth & normals) ?
	//    ALso take LightManager argument? ? can it be replaced for any reason?
	ShadowAtlas(uint32_t size, LightManager &lights);
	~ShadowAtlas();

	bool create();

	// lights smaller than 'min radius' will never cast a shadow
	inline void set_min_radius(float radius) { _min_light_radius = radius; }
	// lights further away than 'max distance' will never cast a shadw
	inline void set_max_distance(float max_distance)
	{
		assert(max_distance > 0);
		_max_distance = std::max(max_distance, 10.f);
		_large_light_radius = _max_distance;
	}
	inline void set_min_change_interval(std::chrono::milliseconds interval) { _min_change_interval = std::max(interval, std::chrono::milliseconds(100)); }

	size_t eval_lights(const std::vector<LightIndex> &relevant_lights, const glm::vec3 &view_pos, const glm::vec3 &view_forward);

	[[nodiscard]] const dense_map<LightID, AtlasLight> &allocated_lights() const { return _id_to_allocated; }

	// calculate a hash that changes if it affects shadow map
	size_t hash_light(const GPULight &light) const;
	bool should_render(const AtlasLight &atlas_light, Time now, size_t hash, bool has_dynamic) const;

	void update_shadow_params();

	void init_slots(size_t count0, size_t count1, size_t count2, bool reserve_sun=false);
	void clear();

	void debug_dump_allocated(bool details=false) const;
	void debug_dump_desired(const std::vector<AtlasLight> &desired_slots) const;

	std::vector<std::pair<SlotSize, size_t>> allocated_counts() const;

	inline size_t slot_size_idx(SlotSize size) const { return _allocator.level_from_size(size) - _allocator.largest_level(); }

	bool remove_allocation(LightID light_id);

private:
	float light_value(const GPULight &light, const glm::vec3 &view_pos, const glm::vec3 &view_forward) const;
	struct Counters
	{
		inline Counters() :
			allocated(0),
			retained(0),
			dropped(0),
			denied(0),
			promoted(0),
			demoted(0),
			change_pending(0)
		{}
		uint32_t allocated;
		uint32_t retained;
		uint32_t dropped;
		uint32_t denied;
		uint32_t promoted;
		uint32_t demoted;
		uint32_t change_pending;

		inline Counters &operator += (const Counters &other)
		{
			allocated += other.allocated;
			retained += other.retained;
			dropped += other.dropped;
			denied += other.denied;
			promoted += other.promoted;
			demoted += other.demoted;
			change_pending += other.change_pending;
			return *this;
		}

		inline uint32_t changed() const { return allocated + dropped + promoted + demoted; }
	};
	struct ValueLight
	{
		float value;
		LightID light_id;
		size_t num_slots;

		inline bool operator > (const ValueLight &that) const { return value > that.value; }
	};

	Counters prioritize_lights(const std::vector<LightIndex> &relevant_lights, const glm::vec3 &view_pos, const glm::vec3 &view_forward, std::vector<ValueLight> &prioritized);
	Counters apply_desired_slots(const std::vector<AtlasLight> &desired_slots, Time now);
	void generate_slots(std::initializer_list<size_t> distribution);
	bool has_slots_available(const AtlasLight &atlas_light, const std::array<size_t, 4> &num_promised) const;
	SlotID alloc_slot(SlotSize size, bool first=true);
	void free_slot(SlotSize size, SlotID node_index);

private:
	LightManager &_lights;  // one could argue that the association should be the other way around...

	dense_map<SlotSize, std::vector<SlotID>> _slot_sets;

	dense_map<LightID, AtlasLight> _id_to_allocated;
	AtlasLight _allocated_sun;

	size_t _max_shadow_slots;

	float _min_light_radius { .5f };
	float _max_distance { 50.f };
	float _large_light_radius { 50.f };

	// shortest interval an allocated slot can change size (toggle)
	std::chrono::milliseconds _min_change_interval;
	small_vec<std::pair<uint32_t, std::chrono::milliseconds>, 8> _render_intervals;

	RGL::buffer::Storage<ShadowSlotInfo> _shadow_slots_info_ssbo;
	small_vec<size_t, 16> _distribution;  // slot sizes of each of the levels (from max to min)

	RGL::SpatialAllocator<uint32_t> _allocator;
};
