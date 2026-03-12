#include "scene.h"

#include "frustum.h"
#include "log.h"

#include <execution>

#include "component_model.h"
#include "component_bounds.h"

using namespace std::chrono;

namespace RGL
{

Scene::Scene(size_t reserve)
{
	_items.reserve(std::max(256ul, reserve));
	// TODO: init the whatever-tree

	_connect_signals();
}

EntityID Scene::add(StaticModel &&model, const component::Transform &transform, bool is_dynamic)
{
	auto model_ent = _entities.create();

	_entities.emplace<component::SphereBounds>(model_ent, model.sphere());
	_entities.emplace<component::Transform>(model_ent, transform);
	_entities.emplace<bool>(model_ent, is_dynamic);  // bad idea
	_entities.emplace<component::Model>(model_ent, std::move(model));

	return model_ent;
}

bool Scene::remove(EntityID entity_id)
{
	_entities.destroy(entity_id);
	return true;
}

void Scene::rebalance(const glm::vec3 &origin)
{
	// TODO: rebalance the whatever-tree
	(void)origin;
}

void Scene::clear()
{
	// don'y fire the connected signals
	_disconnect_signals();
	_entities.clear();
	// TODO: reset the whatever-tree
	_items.clear();

	// reconnect signals again
	_connect_signals();
}

bool Scene::query(const bounds::Sphere &sphere, QueryResult &result) const
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

bool Scene::query(const Frustum &frustum, QueryResult &result) const
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

bool Scene::query(const bounds::AABB &aabb, QueryResult &result) const
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

bool Scene::query(const glm::mat4 &view, const glm::mat4 &ortho, const bounds::AABB &aabb, QueryResult &result) const
{
	if(start_query_maybe(result))
	{
		// TODO: use the whatever-tree instead

		const auto view_proj = ortho * view;

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

bool Scene::start_query_maybe(QueryResult &result) const
{
	const auto now = steady_clock::now();

	if(result.created_at.time_since_epoch().count() == 0 or now - result.created_at > result.max_interval)
	{
		result.static_ids.reserve(_min_result_reserve);
		result.static_ids.clear();
		result.dynamic_ids.reserve(_min_result_reserve);
		result.dynamic_ids.clear();
		result.created_at = now;

		return true;
	}

	return false;
}

void Scene::_disconnect_signals()
{
	for(auto &conn: _signals)
		conn.release();
}

void Scene::_connect_signals()
{
	_signals[0] = _entities.on_construct<component::Model>()    .connect<&Scene::_spatial_insert>(this);
	_signals[1] = _entities.on_update<   component::Transform>().connect<&Scene::_spatial_update>(this);
	_signals[2] = _entities.on_destroy<  component::Model>()    .connect<&Scene::_spatial_remove>(this);
}

void Scene::_spatial_insert(entt::registry &, EntityID entity_id)
{
	const auto &[transform, model, is_dynamic] = _entities.get<component::Transform, component::Model, bool>(entity_id);

	// transform the local bounds into world-space
	auto world_bounds = model.sphere(); // local bounds
	world_bounds.setCenter(glm::mat4(transform) * glm::vec4(world_bounds.center(), 1));
	world_bounds.setRadius(world_bounds.radius() * transform.max_scale());

	_entities.replace<component::SphereBounds>(entity_id, world_bounds);

	// TODO: update the whatever-tree

	// TODO: component with model meta info
	_items[entity_id] = { world_bounds, is_dynamic };
}

void Scene::_spatial_update(entt::registry &e, EntityID entity_id)
{
	if(not _items.contains(entity_id))  // i.e. transform was updated for something without a model
		return;
	_spatial_insert(e, entity_id);
}

void Scene::_spatial_remove(entt::registry &, EntityID entity_id)
{
	_items.erase(entity_id);
}

} // RGL
