#pragma once

#include <cstddef>
#include <entt/entity/registry.hpp>
#include <vector>

#include "bounds.h"
#include "container_types.h"
#include "static_model.h"


#include "component_transform.h"

namespace RGL
{
class Frustum;

struct QueryResult;
using EntityID = entt::entity;
using EntityList = std::vector<EntityID>;


struct QueryResult
{
	static constexpr std::chrono::milliseconds default_query_max_interval { 50 };

	inline QueryResult(std::chrono::milliseconds max_interval_=default_query_max_interval) : max_interval(max_interval_) {}
	// QueryResult(QueryResult &&that);

	EntityList static_ids;
	EntityList dynamic_ids;
	std::chrono::steady_clock::time_point created_at;
	std::chrono::milliseconds max_interval;
	size_t hash { 0 };

	inline size_t size() const { return static_ids.size() + dynamic_ids.size(); }
};

class Scene
{
public:

	struct Item
	{
		bounds::Sphere bounds;
		bool is_dynamic;
	};
	using ItemMap = dense_map<EntityID, Item>;

public:
	Scene(size_t reserve=0);

	inline const entt::registry &entities() const { return _entities; }

	EntityID add(StaticModel &&model, const component::Transform &transform=component::Transform(), bool is_dynamic=false);
	bool remove(EntityID entity_id);

	void rebalance(const glm::vec3 &origin);

	inline size_t size() const { return _items.size(); }
	void clear();

	bool closest(const    glm::vec3 &point,   QueryResult &result) const;

	bool query(const bounds::Sphere &sphere,  QueryResult &result) const;
	bool query(const        Frustum &frustum, QueryResult &result) const;
	bool query(const   bounds::AABB &aabb,    QueryResult &result) const;
	bool query(const glm::mat4 &view, const glm::mat4 &ortho, const bounds::AABB &aabb, QueryResult &result) const;
	// bool query(const bounds::OBB &obb, QueryResult &result);

private:
	void _connect_signals();
	void _disconnect_signals();
	void _spatial_update(entt::registry &, EntityID entity_id);
	void _spatial_insert(entt::registry &, EntityID entity_id);
	void _spatial_remove(entt::registry &, EntityID entity_id);


	bool start_query_maybe(QueryResult &result) const;
	inline void add_result_item(QueryResult &result, EntityID entity_id, const Item &item) const {
		if(item.is_dynamic)
			result.dynamic_ids.push_back(entity_id);
		else
			result.static_ids.push_back(entity_id);
	}

private:
	entt::registry _entities;

	// TODO: some actual acceleration structure here, please :)
	//   but I assume we need a "flat list" to be able to rebuild the whatever-tree when needed?
	ItemMap _items;

	size_t _min_result_reserve { 32 };

	std::array<entt::scoped_connection, 3> _signals;
};

} // RGL
