#pragma once

#include "bounds.h"

#include "container_types.h"

#include <entt/entity/fwd.hpp>


namespace RGL
{
class Frustum;
using EntityID = entt::entity;
using EntityList = std::vector<EntityID>;

struct QueryResult
{
	static constexpr std::chrono::milliseconds default_query_max_interval { 50 };

	inline QueryResult(std::chrono::milliseconds max_interval_=default_query_max_interval) : max_interval(max_interval_) {}
	// QueryResult(QueryResult &&that);

	EntityList static_entities;
	EntityList dynamic_entities;
	std::chrono::steady_clock::time_point created_at;
	std::chrono::milliseconds max_interval;
	size_t hash { 0 };

	enum class SortMode { None, Closest, Farthest } sort_mode { SortMode::None };

	inline size_t size() const { return static_entities.size() + dynamic_entities.size(); }
};

struct SpatialItem
{
	bounds::Sphere bounds;
	bool is_dynamic;
};
using SpatialItems = dense_map<EntityID, SpatialItem>;

class SceneItems
{
	friend class Scene;

public:
	SceneItems();

	bool closest(const    glm::vec3 &point,   QueryResult &result) const;

	bool query(const bounds::Sphere &sphere,  QueryResult &result) const;
	bool query(const        Frustum &frustum, QueryResult &result) const;
	bool query(const   bounds::AABB &aabb,    QueryResult &result) const;
	bool query(const glm::mat4 &view, const glm::mat4 &ortho, const bounds::AABB &aabb, QueryResult &result) const;
	// bool query(const bounds::OBB &obb, QueryResult &result);

	inline size_t numModels() const { return _items.size(); }

	const EntityList &lights() const { return _lights; }
	const EntityList &cameras() const { return _cameras; }


protected:
	bool start_query_maybe(QueryResult &result) const;
	inline void add_result_item(QueryResult &result, EntityID entity_id, const SpatialItem &item) const {
		// TODO: if sort_mode != None, insert sorted
		//   use an std::multi_map, with distance as key?  (i.e. not unordered)
		if(item.is_dynamic)
			result.dynamic_entities.push_back(entity_id);
		else
			result.static_entities.push_back(entity_id);
	}

protected:
	// TODO: some actual acceleration structure here, please :)
	//   but I assume we need a "flat list" to be able to rebuild the whatever-tree when needed?
	SpatialItems _items;
	EntityList _lights;
	EntityList _cameras;

	size_t _min_result_reserve { 32 };
};

} // RGL