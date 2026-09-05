#include "scene_items.h"
#include "component/transform.h"
#include "frustum.h"

#include <glm/mat4x4.hpp>

#include <chrono>
using namespace std::chrono;


namespace RGL
{

SceneItems::SceneItems() :
	_bvh(32)
{
	_idToProxy.reserve(32);
}

static constexpr uint64_t MASK_ALL = std::numeric_limits<uint64_t>::max();

bool SceneItems::query(const bounds::Sphere &sphere, QueryResult &result) const
{
	if(start_query_maybe(result))
	{
		const auto aabb = math::aabb_cast(sphere);

		// verify that the BVH finds the same objects
		_bvh.query(aabb, MASK_ALL, false, [&result](auto, auto, auto meta){

			(meta.is_dynamic? result.dynamic_entities: result.static_entities).push_back(meta.entity_id);
			return true;
		});
		// TODO: verify 'found_by_bvh' contains the same as 'result'

		return true;
	}

	return false;
}

bool SceneItems::query(const Frustum &frustum, QueryResult &result) const
{
	if(start_query_maybe(result))
	{
		auto aabb = frustum.aabb();

		// verify that the BVH finds the same objects
		_bvh.query(aabb, MASK_ALL, false, [&result, &frustum](auto, const auto &item_aabb, auto meta) {

			// check more precisely using the frustum
			if(intersect::check(frustum, item_aabb))
				(meta.is_dynamic? result.dynamic_entities: result.static_entities).push_back(meta.entity_id);
			return true;
		});
		// TODO: objects found by simple check but not by BVH

		return true;
	}

	return false;
}

bool SceneItems::query(const bounds::AABB &aabb, QueryResult &result) const
{
	if(start_query_maybe(result))
	{
		_bvh.query(aabb, MASK_ALL, false, [&result](auto, auto, auto meta) {

			(meta.is_dynamic? result.dynamic_entities: result.static_entities).push_back(meta.entity_id);
			return true;
		});

		return true;
	}

	return false;
}

bool SceneItems::query(const glm::mat4 &view, const glm::mat4 &ortho_proj, const bounds::AABB &aabb, QueryResult &result) const
{
	if(start_query_maybe(result))
	{
		const auto view_proj = ortho_proj * view;

		// std::for_each(std::execution::par_unseq, _items.begin(), _items.end(), [this, &view_proj, &aabb, &result](const auto &item_pair) {
		// 	const auto &[entity_id, item] = item_pair;

		// 	// transform the bounds into given space
		// 	auto bounds = item.bounds;
		// 	bounds.setCenter(view_proj * glm::vec4(item.bounds.center(), 1));

		// 	if(intersect::check(aabb, bounds))
		// 		add_result_item(result, entity_id, item);
		// });

		// for a rough cull, construct a world-space AABB around the prtho projection AABB
		bounds::AABB world_aabb;
		const auto inv_view_proj = glm::inverse(view_proj);
		for(const auto &corner: aabb.corners())
		{
			auto world_corner = inv_view_proj * glm::vec4(corner, 1);
			// perspective divide only necessary for perspective rpojections
			// world_corner.x /= world_corner.w;
			// world_corner.y /= world_corner.w;
			// world_corner.z /= world_corner.w;
			world_aabb.expand(world_corner);
		}

		_bvh.query(world_aabb, MASK_ALL, false, [&result, &aabb, &view_proj](auto, const auto &item_aabb, auto meta) {

			// more precise test by transforming the item's AABB into projection space
			bounds::AABB aabb_proj;
			for(const auto &corner: item_aabb.corners())
			{
				auto corner_proj = view_proj * glm::vec4(corner, 1);
				// perspective divide only necessary for perspective rpojections
				// corner_proj.x /= corner_proj.w;
				// corner_proj.y /= corner_proj.w;
				// corner_proj.z /= corner_proj.w;
				aabb_proj.expand(corner_proj);
			}
			if(intersect::check(aabb, aabb_proj))
				(meta.is_dynamic? result.dynamic_entities: result.static_entities).push_back(meta.entity_id);
			return true;
		});

		return true;
	}

	return false;
}

bool SceneItems::start_query_maybe(QueryResult &result) const
{
	const auto now = steady_clock::now();

	if(result.created_at.time_since_epoch().count() == 0 or now - result.created_at > result.max_interval)
	{
		result.static_entities.reserve(_min_result_reserve);
		result.static_entities.clear();
		result.dynamic_entities.reserve(_min_result_reserve);
		result.dynamic_entities.clear();
		result.created_at = now;

		return true;
	}

	return false;
}

bounds::Sphere transform_bounds(const bounds::Sphere &local_bounds, const component::Transform &transform)
{
	return {
		glm::mat4(transform) * glm::vec4(local_bounds.center(), 1),
		local_bounds.radius() * transform.max_scale(),
	};
}

void SceneItems::spatial_insert(EntityID entity_id, const bounds::Sphere &local_bounds, const component::Transform &transform, bool is_dynamic)
{
	// assert(not _items.contains(entity_id));
	assert(not _idToProxy.contains(entity_id));

	const auto world_bounds = transform_bounds(local_bounds, transform);

	// _items[entity_id] = { world_bounds, is_dynamic };

	const auto aabb = math::aabb_cast(world_bounds);

	auto proxy_id = _bvh.addProxy(aabb, { entity_id, is_dynamic });
	_idToProxy[entity_id] = proxy_id;
}

void SceneItems::spatial_update(EntityID entity_id, const bounds::Sphere &local_bounds, const component::Transform &transform)
{
	// assert(_items.contains(entity_id));
	assert(_idToProxy.contains(entity_id));

	const auto world_bounds = transform_bounds(local_bounds, transform);

	// _items[entity_id].bounds = world_bounds;

	auto found = _idToProxy.find(entity_id);
	if(found != _idToProxy.end())
	{
		const auto proxy_id = found->second;
		const auto aabb = math::aabb_cast(world_bounds);
		_bvh.moveProxy(proxy_id, aabb);
	}
}

void SceneItems::spatial_remove(EntityID entity_id)
{
	// assert(_items.contains(entity_id));
	assert(_idToProxy.contains(entity_id));

	// _items.erase(entity_id);

	auto found = _idToProxy.find(entity_id);
	if(found != _idToProxy.end())
	{
		const auto proxy_id = found->second;
		_bvh.deleteProxy(proxy_id);
		_idToProxy.erase(found);
	}
}

} // RGL