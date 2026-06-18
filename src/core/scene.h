#pragma once

#include <cstddef>
#include <entt/fwd.hpp>
#include <entt/signal/sigh.hpp>
#include <vector>

#include "bounds.h"
#include "container_types.h"
#include "static_model.h"


#include "component/transform.h"

class GPULight;

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

	EntityList static_entities;
	EntityList dynamic_entities;
	std::chrono::steady_clock::time_point created_at;
	std::chrono::milliseconds max_interval;
	size_t hash { 0 };

	enum class SortMode { None, Closest, Farthest } sort_mode { SortMode::None };

	inline size_t size() const { return static_entities.size() + dynamic_entities.size(); }
};

class Scene
{
public:
	struct SpatialItem
	{
		bounds::Sphere bounds;
		bool is_dynamic;
	};
	using ItemMap = dense_map<EntityID, SpatialItem>;

public:
	Scene(entt::registry &entities, size_t reserve=0);

	EntityID add(StaticModel &&model, const component::Transform &transforn, bool is_dynamic=false);
	EntityID add(GPULight &&light, const component::Transform &transfor);

	bool remove(EntityID entity_id);

	void rebalance(const glm::vec3 &origin);

	inline size_t size() const { return _spatial_items.size(); }
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
	inline void add_result_item(QueryResult &result, EntityID entity_id, const SpatialItem &item) const {
		// TODO: if sort_mode != None, insert sorted
		//   use an std::multi_map, with distance as key?  (i.e. not unordered)
		if(item.is_dynamic)
			result.dynamic_entities.push_back(entity_id);
		else
			result.static_entities.push_back(entity_id);
	}

private:
	entt::registry &_entities;

	// TODO: some actual acceleration structure here, please :)
	//   but I assume we need a "flat list" to be able to rebuild the whatever-tree when needed?
	ItemMap _spatial_items;

	size_t _min_result_reserve { 32 };

	std::array<entt::scoped_connection, 3> _signals;
};

} // RGL
