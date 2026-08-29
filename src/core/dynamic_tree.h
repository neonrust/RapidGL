#pragma once

#include <cstdint>
#include <functional>
#include <limits>

#include "bounds.h"

namespace RGL
{

static constexpr uint64_t B3_DEFAULT_CATEGORY_BITS = std::numeric_limits<uint64_t>::max();
static constexpr uint32_t B3_NULL_INDEX = std::numeric_limits<uint32_t>::max();
static constexpr uint64_t B3_DYNAMIC_TREE_VERSION { 0x93EDAF889FD30B4Aull };


struct b3TreeNodeChildren
{
	uint32_t child1; ///< child node index 1
	uint32_t child2; ///< child node index 2
};

enum b3TreeNodeFlags
{
	b3_allocatedNode = 0x0001,
	b3_enlargedNode  = 0x0002,
	b3_leafNode      = 0x0004,
};


struct b3TreeNode
{
	/// The node bounding box
	bounds::AABB aabb; // 24

	/// Category bits for collision filtering
	uint64_t categoryBits { std::numeric_limits<uint64_t>::max() }; // 8

	union
	{
		/// Children (internal node)
		b3TreeNodeChildren children { .child1 = B3_NULL_INDEX, .child2 = B3_NULL_INDEX };

		/// User data (leaf node)
		uint64_t userData;
	}; // 8

	union
	{
		/// The node parent index (allocated node)
		uint32_t parent { B3_NULL_INDEX };

		/// The node freelist next index (free node)
		uint32_t next;
	}; // 4

	/// Height of the node. Leaves have a height of 0.
	uint16_t height { 0 }; // 2

	/// @see b3TreeNodeFlags
	uint16_t flags { b3_allocatedNode }; // 2

	inline bool isLeaf() const
	{
		return (flags & b3_leafNode) > 0;
	}

	inline bool isAllocated() const
	{
		return flags & b3_allocatedNode;
	}
};

struct b3TreeStats
{
	/// Number of internal nodes visited during the query
	int nodeVisits;

	/// Number of leaf nodes visited during the query
	int leafVisits;
};

struct b3RayCastInput
{
	glm::vec3 origin;

	/// Translation of the ray cast.
	/// end = start + translation.  (i.e. direction * length)
	glm::vec3 translation;

	/// The maximum fraction of the translation to consider, typically 1
	float maxFraction;
};

struct b3BoxCastInput
{
	/// The AABB to cast, in the tree's frame.
	bounds::AABB box;

	/// The sweep translation.
	glm::vec3 translation;

	/// The maximum fraction of the translation to consider, typically 1.
	float maxFraction;
};

class b3DynamicTree // -> BVHTree
{
public:
	using ProxyID = uint32_t;
	using NodeIndex = uint32_t;

	using QueryCb = std::function<bool(ProxyID, uint64_t /*userData*/)>;
	using QueryClosestCb = std::function<float(float, ProxyID, uint64_t /*userData*/)>;
	using CastRayCb = std::function<float(const b3RayCastInput &, ProxyID, uint64_t /*userData*/)>;
	using CastBoxCb = std::function<float(const b3BoxCastInput &, ProxyID, uint64_t /*userData*/)>;


public:
	b3DynamicTree(uint32_t proxyCapacity=0); // b3DynamicTree_Create
	~b3DynamicTree(); // b3DynamicTree_Destroy

	ProxyID newProxy(bounds::AABB aabb, uint64_t categoryBits, uint64_t userData); // b3DynamicTree_CreateProxy
	void deleteProxy(ProxyID id); // b3DynamicTree_DestroyProxy
	void moveProxy(ProxyID id, bounds::AABB aabb); // b3DynamicTree_MoveProxy
	void growProxy(ProxyID id, bounds::AABB aabb); // b3DynamicTree_EnlargeProxy
	void setCategoryBits(ProxyID id, uint64_t categoryBits); // b3DynamicTree_SetCategoryBits
	[[nodiscard]] uint64_t categoryBits(ProxyID id) const; // b3DynamicTree_GetCategoryBits

	b3TreeStats query(bounds::AABB aabb, uint64_t maskBits, bool requireAllBits, QueryCb callback) const; // b3TreeStats b3DynamicTree_Query
	b3TreeStats queryClosest(glm::vec3 point, uint64_t maskBits, bool requireAllBits, QueryClosestCb callback, float &minDistanceSqr) const; // b3DynamicTree_QueryClosest
	b3TreeStats castRay(const b3RayCastInput &input, uint64_t maskBits, bool requireAllBits, CastRayCb callback) const; // b3TreeStats b3DynamicTree_RayCast
	b3TreeStats castBox(const b3BoxCastInput &input, uint64_t maskBits, bool requireAllBits, CastBoxCb callback) const;

	[[nodiscard]] int32_t height() const; // b3DynamicTree_GetHeight
	[[nodiscard]] float areaRatio() const; // b3DynamicTree_GetAreaRatio

	[[nodiscard]] bounds::AABB rootBounds() const; // b3DynamicTree_GetRootBounds

	[[nodiscard]] inline uint32_t numProxies() const { return _proxyCount; } // b3DynamicTree_GetProxyCount

	// TODO: uint32_t rebuild(bool full); // b3DynamicTree_Rebuild

	[[nodiscard]] size_t byteSize() const; // b3DynamicTree_GetByteCount

	void validate(); // b3DynamicTree_Validate

	void validateGrown(); // b3DynamicTree_ValidateNoEnlarged

	/// Save this tree to a file for debugging
	// void b3DynamicTree_Save( const b3DynamicTree* tree, const char* fileName );

	/// Load a file for debugging
	// b3DynamicTree b3DynamicTree_Load( const char* fileName, float scale );

	[[nodiscard]] int computeHeight() const;

private:
	NodeIndex allocateNode();
	void freeNode(uint32_t nodeId);

	NodeIndex findBestSibling(bounds::AABB box);

	void rotateNodes(uint32_t iA);
	void insertLeaf(NodeIndex leaf, bool shouldRotate=false);
	void removeLeaf(NodeIndex leaf);

	[[nodiscard]] int computeHeightRecurse(NodeIndex nodeId) const;
	void validateStructure(NodeIndex index) const;
	void validateMetrics(NodeIndex index) const;
	void validateNoGrown() const;

	float nodeDistanceSq(glm::vec3 point, const b3TreeNode &node) const;

	// TODO: NodeIndex buildTree(uint32_t leafCount);
	// TODO: uint32_t partitionMid(NodeIndex *indices, glm::vec3 *centers, uint32_t count);
	// TODO: uint32_t b3PartitionSAH(NodeIndex *indices, NodeIndex *binIndices, bounds::AABB *boxes, uint32_t count);

private:
	/// The dynamic tree _version. Always the first field. Useful
	/// if the tree is serialized.
	uint64_t _version;

	/// The tree _nodes
	std::vector<b3TreeNode> _nodes;

	/// The _root index
	NodeIndex _root;

	/// The number of nodes
	uint32_t _nodeCount;

	/// The allocated node space
	uint32_t _nodeCapacity;

	/// Number of proxies created
	uint32_t _proxyCount;

	/// Node free list
	NodeIndex _freeList;

/* TODO: rebuild
	/// Leaf indices for rebuild
	std::vector<NodeIndex> leafIndices;

	/// Leaf bounding boxes for rebuild
	std::vector<bounds::AABB> leafBoxes;

	/// Leaf bounding box centers for rebuild
	std::vector<glm::vec3> leafCenters;

	/// Bins for sorting during rebuild
	std::vector<int> binIndices;

	/// Allocated space for rebuilding
	int rebuildCapacity;
*/
};

#if 0
/// Get proxy user data
B3_INLINE uint64_t b3DynamicTree_GetUserData( const b3DynamicTree* tree, int proxyId )
{
	return tree->nodes[proxyId].userData;
}

/// Get the AABB of a proxy
B3_INLINE b3AABB b3DynamicTree_GetAABB( const b3DynamicTree* tree, int proxyId )
{
	return tree->nodes[proxyId].aabb;
}
#endif

/**@}*/ // tree

} // RGL