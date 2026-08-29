#include "scene_items.h"
#include "frustum.h"

#include <glm/mat4x4.hpp>

#include <execution>
#include <chrono>
using namespace std::chrono;


namespace RGL
{

SceneItems::SceneItems()
{

}

bool SceneItems::query(const bounds::Sphere &sphere, QueryResult &result) const
{
	if(start_query_maybe(result))
	{
		// TODO: use the whatever-tree instead

		std::for_each(std::execution::par_unseq, _items.begin(), _items.end(), [this, &sphere, &result](const auto &item_pair) {
			const auto &[entity_id, item] = item_pair;
			if(intersect::check(sphere, item.bounds))
				add_result_item(result, entity_id, item);
		});

		return true;
	}

	return false;
}

bool SceneItems::query(const Frustum &frustum, QueryResult &result) const
{
	if(start_query_maybe(result))
	{
		// TODO: use the whatever-tree instead

		std::for_each(std::execution::par_unseq, _items.begin(), _items.end(), [this, &frustum, &result](const auto &item_pair) {
			const auto &[entity_id, item] = item_pair;
			if(intersect::check(frustum, item.bounds))
				add_result_item(result, entity_id, item);
		});

		return true;
	}

	return false;
}

bool SceneItems::query(const bounds::AABB &aabb, QueryResult &result) const
{
	if(start_query_maybe(result))
	{
		// TODO: use the whatever-tree instead

		std::for_each(std::execution::par_unseq, _items.begin(), _items.end(), [this, &aabb, &result](const auto &item_pair) {
			const auto &[entity_id, item] = item_pair;
			if(intersect::check(aabb, item.bounds))
				add_result_item(result, entity_id, item);
		});

		return true;
	}

	return false;
}

bool SceneItems::query(const glm::mat4 &view, const glm::mat4 &proj, const bounds::AABB &aabb, QueryResult &result) const
{
	if(start_query_maybe(result))
	{
		// TODO: use the whatever-tree instead

		const auto view_proj = proj * view;

		std::for_each(std::execution::par_unseq, _items.begin(), _items.end(), [this, &view_proj, &aabb, &result](const auto &item_pair) {
			const auto &[entity_id, item] = item_pair;

				   // transform the bounds into given space
			auto bounds = item.bounds;
			bounds.setCenter(view_proj * glm::vec4(item.bounds.center(), 1));

			if(intersect::check(aabb, bounds))
				add_result_item(result, entity_id, item);
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

} // RGL