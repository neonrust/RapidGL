#pragma once

#include "bounds.h"
#include "rendertarget_2d.h"
#include "container_types.h"
#include "spatial_allocator.h"
#include "lights.h"
#include "ssbo.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "generated/shared-structs.h"

struct GPULight;

namespace RGL
{
class LightManager;
struct LightWrapper;
class Camera;
class Scene;
struct QueryResult;

using Time = std::chrono::steady_clock::time_point;

class ShadowAtlas : public RenderTarget::Texture2d
{
public:
	enum class CubeFace : uint32_t { PosX, NegX, PosY, NegY, PosZ, NegZ };

	static constexpr uint32_t MAX_CASCADES = 4;
	static constexpr uint32_t MAX_SLOTS= 6;
	static_assert(MAX_CASCADES >= 1 and MAX_CASCADES <= MAX_SLOTS);
	static_assert(MAX_SLOTS >= MAX_CASCADES and MAX_SLOTS >= 6);

private:
	using allocator_t = SpatialAllocator<uint32_t>;

public:
	using SlotSize = allocator_t::SizeT;
	using SlotID = allocator_t::NodeIndex;
	using SlotMask = uint_fast8_t;
	static constexpr SlotMask SlotMaskAll { 0xff };

	struct SlotDef
	{
		SlotSize size;
		SlotID node_index;
		glm::uvec4 rect;    // TODO: can get by with x, y + size ?
	};
	static_assert(sizeof(SlotDef) == 24);
	enum class SlotConfig : uint_fast8_t
	{
		Cube     = 6,
		Single   = 1,
		Cascades = 3,
	};
	struct AtlasLight
	{
		inline AtlasLight() : uuid(NO_LIGHT_ID), _dirty(true) {}
		AtlasLight(const AtlasLight &other);
		AtlasLight &operator = (const AtlasLight &other) = default;

		inline operator bool () const { return uuid != NO_LIGHT_ID; }

		inline bool is_dirty() const { return _dirty; }

		inline void set_dirty() const { _dirty = true; }       // called from allocated_lights(); const
		inline void on_rendered(Time t, uint32_t new_hash) const // called from allocated_lights(); const
		{
			_dirty = false;
			_last_rendered = t;
			hash = new_hash;
			_frames_skipped = 0;
		}

		LightID uuid;
		SlotConfig slot_config { SlotConfig::Single };
		uint_fast8_t num_slots;
		std::array<SlotDef, 6> slots; // per slot
		mutable uint32_t hash { 0 };

	private:
		mutable bool _dirty { true };
		mutable Time _last_rendered;   // TODO: define a "pixels per second" limit to dictate a limit for how many shadow map slots may be updated (in light-value/age order?)
		mutable uint32_t _frames_skipped { 0 };
		Time _last_size_change;

		friend class ShadowAtlas;
	};
	static_assert(sizeof(AtlasLight) == 184);

	struct CSMParams
	{
		uint32_t num_cascades { 0 };  // valid items in arrays
		std::array<float, MAX_CASCADES> split_depth;
		std::array<glm::mat4, MAX_CASCADES> light_view;
		std::array<glm::mat4, MAX_CASCADES> light_projection;
		std::array<glm::mat4, MAX_CASCADES> light_view_projection;
		// std::array<glm::vec2, MAX_CASCADES> depth_range;
		std::array<glm::vec2, MAX_CASCADES> near_far_plane;
		std::array<bounds::AABB, MAX_CASCADES> view_aabb;
		float light_radius_uv;

		inline operator bool () const { return num_cascades >= 1 and num_cascades <= 4; }
		inline void clear() { num_cascades = 0; }
	};

public:

	// TODO: specify which channels to use (e.g. depth & normals) ?
	//    ALso take LightManager argument? ? can it be replaced for any reason?
	ShadowAtlas(uint32_t size, LightManager &lights);
	~ShadowAtlas();

	bool create();

	// lights smaller than 'min radius' will never cast a shadow
	inline void set_min_radius(float radius) { _min_light_radius = radius; }
	// lights further away than 'max distance' will never cast a shadow
	inline void set_max_distance(float max_distance)
	{
		assert(max_distance > 0);
		_max_distance = std::max(max_distance, 10.f);
		_large_light_radius = _max_distance;
	}
	inline void set_min_change_interval(std::chrono::milliseconds interval) { _min_change_interval = std::max(interval, std::chrono::milliseconds(100)); }
	inline void set_sun_cascades(uint_fast8_t num_cascades) {
		assert(num_cascades >= 1 and num_cascades <= MAX_CASCADES);
		_sun_num_cascades = num_cascades;
	}
	inline float csm_backoff() const { return _csm_cascade_backoff; }
	inline void set_csm_backoff(float backoff) { _csm_cascade_backoff = backoff; }
	inline bool csm_stabilization() const { return _csm_stabilization; }
	inline void set_csm_stabilization(bool enabled) { _csm_stabilization = enabled; }

	uint32_t update_allocations(const std::vector<LightIndex> &relevant_lights, const glm::vec3 &view_pos, const glm::vec3 &view_forward);

	[[nodiscard]] const dense_map<LightID, AtlasLight> &allocated_lights() const { return _id_to_allocated; }
	[[nodiscard]] SlotMask need_render(const AtlasLight &atlas_light, Time now, size_t hash, const Scene &scene) const;

	void update_slots_ssbo();
	const CSMParams &update_csm_params(LightID light_id, const Camera &camera);//, float radius_uv=0.5f);
	inline const CSMParams &csm_params() const { return _csm_params; }

	void clear();

	void debug_dump_allocated(bool details=false) const;
	void debug_dump_desired(const std::vector<AtlasLight> &desired_slots) const;
	void debug_dump_available();

	[[nodiscard]] std::vector<std::pair<SlotSize, uint32_t>> allocated_counts() const;

	[[nodiscard]] inline uint32_t slot_size_idx(SlotSize size) const { return _allocator.level_from_size(size) - _allocator.largest_level(); }

	bool remove_allocation(LightID light_id);

	const QueryResult &pvs(const Scene &scene, LightID light_id, uint_fast8_t slot_idx=0) const;

private:
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
		SlotConfig config;

		inline bool operator > (const ValueLight &that) const { return value > that.value; }
	};
	static_assert(sizeof(ValueLight) == 12);
	struct SlotsSet
	{
		small_vec<uint32_t, 6>            distribution;  // slot counts for each of the levels (from largest to smallest)
		small_vec<std::vector<SlotID>, 6> available_slots;
		uint32_t total_num_slots { 0 };
	};
	enum SlotSetCategory { NoSunSlots, WithSunSlots };

	void evaluate_lights(const std::vector<LightIndex> &relevant_lights, const glm::vec3 &view_pos, const glm::vec3 &view_forward, std::vector<ValueLight> &prioritized, dense_set<LightID> &seen_lights);
	float evaluate_light(const bounds::Sphere &light_sphere, const glm::vec3 &view_pos, const glm::vec3 &view_forward) const;
	Counters compute_desired(const std::vector<ValueLight> &valued_lights, std::vector<AtlasLight> &desired_slots);
	Counters apply_desired_slots(const std::vector<AtlasLight> &desired_slots, Time now);
	void log_changes(const Counters &counters, size_t num_prio, Time start_time);
	void generate_slots(std::initializer_list<uint32_t> distribution, SlotSetCategory slots_cat);
	bool has_slots_available(const AtlasLight &atlas_light, const small_vec<uint32_t, 6> &num_promised) const;
	SlotID alloc_slot(SlotSize slot_size, bool first=true);
	void free_slot(SlotSize slot_size, SlotID node_index);
	void free_all_slots(const AtlasLight &atlas_light);
	struct LightViewProjection
	{
		glm::mat4 projection;
		glm::mat4 view;
		float near_z;
		float far_z;
	};
	LightViewProjection light_view_projection(const LightWrapper &light, uint32_t idx=0) const;
	std::string sizes_count_summary(const AtlasLight &atlas_light) const;
	std::string sizes_count_summary(const small_vec<uint32_t, 6> &size_counts) const;

private:
	LightManager &_lights;  // one could argue that the association should be the other way around...

	SlotSetCategory _current_slot_set { NoSunSlots };
	std::array<SlotsSet, 2> _slots_sets;
	inline small_vec<uint32_t, 6> &curr_distribution() { return _slots_sets[_current_slot_set].distribution; }
	inline const small_vec<uint32_t, 6> &curr_distribution() const { return _slots_sets[_current_slot_set].distribution; }
	inline small_vec<std::vector<SlotID>, 6> &curr_available() { return _slots_sets[_current_slot_set].available_slots; }
	inline const small_vec<std::vector<SlotID>, 6> &curr_available() const { return _slots_sets[_current_slot_set].available_slots; }
	void switch_slots_set(SlotSetCategory to);

	dense_map<LightID, AtlasLight> _id_to_allocated;

	uint_fast8_t _sun_num_cascades { 3 };
	float _csm_frustum_split_mix { 0.55f };  // 0 = linear, 1 = logarithic
	float _csm_cascade_backoff { 4.f };
	bool _csm_stabilization { true };
	float _csm_light_radius_uv { 0.5f };
	CSMParams _csm_params;

	float _min_light_radius { .5f };
	float _max_distance { 50.f };
	float _large_light_radius { 50.f };

	// shortest interval an allocated slot can change size (toggle)
	std::chrono::milliseconds _min_change_interval;
	small_vec<std::pair<uint32_t, std::chrono::milliseconds>, 8> _render_intervals;

	buffer::Storage<ShadowSlotInfo> _shadow_slots_info_ssbo;

	mutable dense_map<uint32_t, QueryResult> _light_pvs;

	SpatialAllocator<uint32_t> _allocator;
};

} // RGL
