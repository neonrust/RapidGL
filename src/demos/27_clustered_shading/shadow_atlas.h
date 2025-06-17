#pragma once

#include "rendertarget_2d.h"
#include "container_types.h"
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "spatial_allocator.h"
#include "light_manager.h"
#include "lights.h"

namespace RGL
{

using LightID = uint32_t;
using Time = std::chrono::steady_clock::time_point;

class ShadowAtlas : public RenderTarget::Texture2d
{
public:
	static constexpr size_t PADDING = 1;
	enum class CubeFace : uint32_t { PosX, NegX, PosY, NegY, PosZ, NegZ };

private:
	SpatialAllocator<uint32_t> _allocator;

public:
	using AllocatedIndex = decltype(_allocator)::NodeIndex;
	using SizeT = decltype(_allocator)::SizeT;

	struct AtlasLight
	{
		LightID uuid;
		size_t num_shadow_maps;
		float importance;
		SizeT slot_size;        // 0 if no slot assigned
		SizeT prev_slot_size;
		std::array<AllocatedIndex, 6> map_node;
		std::array<glm::uvec4, 6> rect;
		Time last_used;
		Time last_updated;
		Time last_size_change;
	};

public:
	ShadowAtlas(uint32_t size);
	~ShadowAtlas();

	void set_importance_func(std::function<float(const GPULight &)> func) { _importance = func; }

	bool create();

	//const Slot &point_light(LightID uuid, float importance=1);

	void eval_lights(const LightManager &lights);

private:
	//dense_map<LightID, Slot> _lights;
	small_vec<std::tuple<decltype(_allocator)::SizeT, size_t>> _distribution;

	dense_map<float, LightID> _prioritized;
	dense_map<LightID, AtlasLight> _allocated;
	size_t _max_shadow_casters;

	std::function<float(const GPULight &)> _importance;
};

} //  RGL
