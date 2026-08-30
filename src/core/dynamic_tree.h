// This is bsaically a straight port of box3d's DynamicTree to C++
// https://github.com/erincatto/box3d
//
// It has been made into a template on the user data type (thus far).

#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <cstring>

#include "bounds.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

namespace RGL
{


struct DynamicTreeStats
{
	uint32_t nodeVisits;
	uint32_t leafVisits;
};

struct TreeRayCastInput
{
	glm::vec3 origin;
	glm::vec3 translation;
	float maxFraction;
};

struct TreeBoxCastInput
{
	bounds::AABB box;
	glm::vec3 translation;
	float maxFraction;
};

using TreeProxyID = uint32_t;
using TreeNodeIndex = uint32_t;

static constexpr uint64_t      DT_DEFAULT_CATEGORY_BITS = std::numeric_limits<uint64_t>::max();
static constexpr TreeNodeIndex DT_NULL_INDEX =            std::numeric_limits<uint32_t>::max();
static constexpr uint64_t      DT_DYNAMIC_TREE_VERSION { 0x3ae7c89ceb448f33ull };


struct DynamicTreeNodeChildren
{
	TreeNodeIndex child1;
	TreeNodeIndex child2;
};

template<typename UserT=uint64_t>
struct DynamicTreeNode
{
	bounds::AABB aabb;

	uint64_t categoryBits { std::numeric_limits<uint64_t>::max() };

	union
	{
		DynamicTreeNodeChildren children { .child1 = DT_NULL_INDEX, .child2 = DT_NULL_INDEX };
		UserT userData;
	};

	union
	{
		TreeNodeIndex parent { DT_NULL_INDEX };
		TreeNodeIndex next;
	};

	uint16_t height { 0 };

	enum class Flag : uint16_t
	{
		allocatedNode = 0x0001,
		enlargedNode  = 0x0002,
		leafNode      = 0x0004,
	};

	Flag flags { Flag::allocatedNode };

	inline bool isLeaf() const      { return (flags & Flag::leafNode) > 0; }
	inline bool isAllocated() const { return (flags & Flag::allocatedNode) > 0; }
};

template<typename UserT=uint64_t>
class DynamicTree
{
public:
	using QueryCb = std::function<bool(TreeProxyID, UserT)>;
	using QueryClosestCb = std::function<float(float, TreeProxyID, UserT)>;
	using CastRayCb = std::function<float(const TreeRayCastInput &, TreeProxyID, UserT)>;
	using CastBoxCb = std::function<float(const TreeBoxCastInput &, TreeProxyID, UserT)>;

public:
	DynamicTree(uint32_t proxyCapacity=0); // b3DynamicTree_Create
	~DynamicTree(); // b3DynamicTree_Destroy

	TreeProxyID addProxy(bounds::AABB aabb, uint64_t categoryBits, UserT userData); // b3DynamicTree_CreateProxy
	void deleteProxy(TreeProxyID id); // b3DynamicTree_DestroyProxy
	void moveProxy(TreeProxyID id, bounds::AABB aabb); // b3DynamicTree_MoveProxy
	void growProxy(TreeProxyID id, bounds::AABB aabb); // b3DynamicTree_EnlargeProxy
	void setCategoryBits(TreeProxyID id, uint64_t categoryBits); // b3DynamicTree_SetCategoryBits
	[[nodiscard]] uint64_t categoryBits(TreeProxyID id) const; // b3DynamicTree_GetCategoryBits
	[[nodiscard]] inline uint32_t numProxies() const { return _proxyCount; } // b3DynamicTree_GetProxyCount
	[[nodiscard]] UserT proxyUserData(TreeProxyID id) const;
	[[nodiscard]] bounds::AABB proxyAABB(TreeProxyID id) const;

	DynamicTreeStats query(bounds::AABB aabb, uint64_t maskBits, bool requireAllBits, QueryCb callback) const; // DynamicTreeStats b3DynamicTree_Query
	DynamicTreeStats queryClosest(glm::vec3 point, uint64_t maskBits, bool requireAllBits, QueryClosestCb callback, float &minDistanceSqr) const; // b3DynamicTree_QueryClosest
	DynamicTreeStats castRay(const TreeRayCastInput &input, uint64_t maskBits, bool requireAllBits, CastRayCb callback) const; // DynamicTreeStats b3DynamicTree_RayCast
	DynamicTreeStats castBox(const TreeBoxCastInput &input, uint64_t maskBits, bool requireAllBits, CastBoxCb callback) const;

	[[nodiscard]] int32_t height() const; // b3DynamicTree_GetHeight
	[[nodiscard]] float areaRatio() const; // b3DynamicTree_GetAreaRatio

	[[nodiscard]] bounds::AABB rootBounds() const; // b3DynamicTree_GetRootBounds

	[[nodiscard]] size_t byteSize() const; // b3DynamicTree_GetByteCount

	// TODO: uint32_t rebuild(bool full); // b3DynamicTree_Rebuild


#if defined(TREE_VALIDATION)
	[[nodiscard]] int computeHeight() const;
	void validate(); // b3DynamicTree_Validate
	void validateGrown(); // b3DynamicTree_ValidateNoEnlarged
#endif

private:
	TreeNodeIndex allocateNode();
	void freeNode(uint32_t nodeId);

	TreeNodeIndex findBestSibling(bounds::AABB box);

	void rotateNodes(uint32_t iA);
	void insertLeaf(TreeNodeIndex leaf, bool shouldRotate=false);
	void removeLeaf(TreeNodeIndex leaf);

#if defined(TREE_VALIDATION)
	[[nodiscard]] int computeHeightRecurse(TreeNodeIndex nodeId) const;
	void validateStructure(TreeNodeIndex index) const;
	void validateMetrics(TreeNodeIndex index) const;
	void validateNoGrown() const;
#endif
	float nodeDistanceSq(glm::vec3 point, const DynamicTreeNode<UserT> &node) const;

	// TODO: NodeIndex buildTree(uint32_t leafCount);
	// TODO: uint32_t partitionMid(NodeIndex *indices, glm::vec3 *centers, uint32_t count);
	// TODO: uint32_t b3PartitionSAH(NodeIndex *indices, NodeIndex *binIndices, bounds::AABB *boxes, uint32_t count);

private:
	uint64_t _version;
	std::vector<DynamicTreeNode<UserT>> _nodes;
	TreeNodeIndex _root;
	uint32_t _nodeCount;
	uint32_t _nodeCapacity;
	uint32_t _proxyCount;
	TreeNodeIndex _freeList;

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

template<typename UserT>
DynamicTree<UserT>::DynamicTree(uint32_t proxyCapacity) :
	_version(DT_DYNAMIC_TREE_VERSION),
	_root(DT_NULL_INDEX),
	_nodeCount(0),
	_nodeCapacity(0),
	_proxyCount(0),
	_freeList(0)
// TODO: rebuild rebuildCapacity(0)
{
	const auto capacity = std::max(proxyCapacity, 16u); // at least 16 proxies

	_nodeCapacity = 2u * capacity - 1u;
	_nodes.resize(_nodeCapacity);

	std::memset(_nodes.data(), 0, _nodes.size() * sizeof(DynamicTreeNode<UserT>));

	// Build a linked list for the free list.
	// todo use a bump allocator until the capacity is consumed (see b3PoolAllocator)
	for(auto idx = 0u; idx < _nodeCapacity - 2; ++idx)
		_nodes[idx].next = idx + 1u;
	_nodes.back().next = DT_NULL_INDEX;
}

template<typename UserT>
inline DynamicTree<UserT>::~DynamicTree()
{
}

template<typename UserT>
TreeProxyID DynamicTree<UserT>::addProxy(bounds::AABB aabb, uint64_t categoryBits, UserT userData)
{
	assert(math::valid(aabb));

	auto proxyId = allocateNode();
	auto &node = _nodes[proxyId];

	node.aabb = aabb;
	node.userData = userData;
	node.categoryBits = categoryBits;
	node.height = 0;
	node.flags = DynamicTreeNode<UserT>::Flag::allocatedNode
		| DynamicTreeNode<UserT>::Flag::leafNode;

	bool shouldRotate = true;
	insertLeaf(proxyId, shouldRotate);

	++_proxyCount;

	return proxyId;
}

template<typename UserT>
void DynamicTree<UserT>::deleteProxy(TreeProxyID proxyId)
{
	assert(0 <= proxyId and proxyId < _nodeCapacity);
	assert(_nodes[proxyId].isLeaf());

	removeLeaf(proxyId);
	freeNode(proxyId);

	assert(_proxyCount > 0);
	--_proxyCount;
}

template<typename UserT>
void DynamicTree<UserT>::moveProxy(TreeProxyID proxyId, bounds::AABB aabb)
{
	assert(math::valid(aabb));
	assert(0 <= proxyId and proxyId < _nodeCapacity );
	assert(_nodes[proxyId].isLeaf());

	removeLeaf(proxyId);

	_nodes[proxyId].aabb = aabb;

	bool shouldRotate = false;
	insertLeaf(proxyId, shouldRotate);
}

template<typename UserT>
void DynamicTree<UserT>::growProxy(TreeProxyID proxyId, bounds::AABB aabb)
{
	assert(math::valid(aabb));
	assert(0 <= proxyId and proxyId < _nodeCapacity);
	assert(_nodes[proxyId].isLeaf());

	// Caller must ensure this

	assert(not intersect::check(_nodes[proxyId].aabb, aabb));

	auto &node = _nodes[proxyId];
	node.aabb = aabb;

	auto parentIndex = node.parent;
	while(parentIndex != DT_NULL_INDEX)
	{
		auto node = _nodes[parentIndex];
		bool changed = node.aabb.expand(aabb);

		// todo not sure why this node is marked as enlarged even if it didn't change
		node.flags |= DynamicTreeNode<UserT>::Flag::enlargedNode;

		parentIndex = node.parent;

		if(not changed)
			break;
	}

	while(parentIndex != DT_NULL_INDEX)
	{
		auto node = _nodes[parentIndex];
		if(node.flags & DynamicTreeNode<UserT>::Flag::enlargedNode)
			// early out because this ancestor was previously ascended and marked as enlarged
			break;

		node.flags |= DynamicTreeNode<UserT>::Flag::enlargedNode;
		parentIndex = node.parent;
	}
}

template<typename UserT>
void DynamicTree<UserT>::setCategoryBits(TreeProxyID proxyId, uint64_t categoryBits)
{
	assert(_nodes[proxyId].isLeaf());

	_nodes[proxyId].categoryBits = categoryBits;

	// Fix up category bits in ancestor internal _nodes
	auto nodeIndex = _nodes[proxyId].parent;
	while(nodeIndex != DT_NULL_INDEX )
	{
		auto &node = _nodes[nodeIndex];
		auto child1 = node.children.child1;
		assert(child1 != DT_NULL_INDEX);
		auto child2 = node.children.child2;
		assert(child2 != DT_NULL_INDEX);
		node.categoryBits = _nodes[child1].categoryBits | _nodes[child2].categoryBits;

		nodeIndex = node.parent;
	}
}

template<typename UserT>
uint64_t DynamicTree<UserT>::categoryBits(TreeProxyID proxyId) const
{
	assert( 0 <= proxyId and proxyId < _nodeCapacity);
	return _nodes[proxyId].categoryBits;
}

template<typename UserT>
int DynamicTree<UserT>::height() const
{
	if(_root == DT_NULL_INDEX)
		return 0;

	return _nodes[_root].height;
}

template<typename UserT>
float DynamicTree<UserT>::areaRatio() const
{
	if(_root == DT_NULL_INDEX)
		return 0;

	float rootArea = _nodes[_root].aabb.surfaceArea();

	float totalArea = 0;
	for(auto i = 0u; i < _nodeCapacity; ++i )
	{
		const auto &node = _nodes[i];
		if(not node.isAllocated() or node.isLeaf() or i == _root)
			continue;

		totalArea += node.aabb.surfaceArea();
	}

	return totalArea / rootArea;
}

template<typename UserT>
bounds::AABB DynamicTree<UserT>::rootBounds() const
{
	if(_root != DT_NULL_INDEX)
		return _nodes[_root].aabb;

	return {};
}

template<typename UserT>
static DynamicTreeNode<UserT> b3_defaultTreeNode = {
	.aabb = { { 0, 0, 0 }, { 0, 0, 0 } },
	.categoryBits = DT_DEFAULT_CATEGORY_BITS,
	.children = {
		.child1 = DT_NULL_INDEX,
		.child2 = DT_NULL_INDEX,
	},
	.parent = DT_NULL_INDEX,
	.height = 0,
	.flags = DynamicTreeNode<UserT>::Flag::allocatedNode,
};

// Allocate a node from the pool. Grow the pool if necessary.
template<typename UserT>
TreeNodeIndex DynamicTree<UserT>::allocateNode()
{
	// Expand the node pool as needed.
	if(_freeList == DT_NULL_INDEX )
	{
		assert(_nodeCount == _nodeCapacity);

		// The free list is empty. Rebuild a bigger pool.
		// DynamicTreeNode* oldNodes = tree->nodes;
		_nodeCapacity += _nodeCapacity >> 1;
		_nodes.resize(_nodeCapacity);
		// zero the new nodes
		std::memset(&_nodes[_nodeCount], 0, (_nodeCapacity - _nodeCount) * sizeof(decltype(_nodes)::value_type));

		// Build a linked list for the free list. The parent pointer becomes the "next" pointer.
		// todo avoid building freelist?
		for(auto idx = _nodeCount; idx < _nodeCapacity - 1; ++idx )
			_nodes[idx].next = idx + 1;
		_nodes.back().next = DT_NULL_INDEX;

		_freeList = _nodeCount;
	}

	// Peel a node off the free list.
	const auto nodeIndex = _freeList;
	auto &node = _nodes[size_t(nodeIndex)];
	_freeList = node.next;
	node = b3_defaultTreeNode<UserT>();
	++_nodeCount;

	return nodeIndex;
}

template<typename UserT>
void DynamicTree<UserT>::freeNode(uint32_t nodeId)
{
	assert( 0 <= nodeId and nodeId < _nodeCapacity );
	assert( 0 < _nodeCount );
	_nodes[nodeId].next = _freeList;
	_nodes[nodeId].flags = 0;
	_freeList = nodeId;
	--_nodeCount;
}

template<typename UserT>
void DynamicTree<UserT>::insertLeaf(TreeNodeIndex leaf, bool shouldRotate)
{
	if(_root == DT_NULL_INDEX )
	{
		_root = leaf;
		_nodes[_root].parent = DT_NULL_INDEX;
		return;
	}

		   // Stage 1: find the best sibling for this node
	auto leafAABB = _nodes[leaf].aabb;
	auto sibling = findBestSibling(leafAABB);

		   // Stage 2: create a new parent for the leaf and sibling
	auto oldParent = _nodes[sibling].parent;
	auto newParent = allocateNode();

		   // warning: node pointer can change after allocation
	_nodes[newParent].parent = oldParent;
	_nodes[newParent].userData = std::numeric_limits<uint64_t>::max();
	_nodes[newParent].aabb = math::envelop(leafAABB, _nodes[sibling].aabb);
	_nodes[newParent].categoryBits = _nodes[leaf].categoryBits | _nodes[sibling].categoryBits;
	_nodes[newParent].height = _nodes[sibling].height + 1;

	if(oldParent != DT_NULL_INDEX)
	{
		// The sibling was not the root.
		if(_nodes[oldParent].children.child1 == sibling)
			_nodes[oldParent].children.child1 = newParent;
		else
			_nodes[oldParent].children.child2 = newParent;

		_nodes[newParent].children.child1 = sibling;
		_nodes[newParent].children.child2 = leaf;
		_nodes[sibling].parent = newParent;
		_nodes[leaf].parent = newParent;
	}
	else
	{
		// The sibling was the root.
		_nodes[newParent].children.child1 = sibling;
		_nodes[newParent].children.child2 = leaf;
		_nodes[sibling].parent = newParent;
		_nodes[leaf].parent = newParent;
		_root = newParent;
	}

		   // Stage 3: walk back up the tree fixing heights and AABBs
	auto index = _nodes[leaf].parent;
	while(index != DT_NULL_INDEX)
	{
		auto child1 = _nodes[index].children.child1;
		auto child2 = _nodes[index].children.child2;

		assert(child1 != DT_NULL_INDEX);
		assert(child2 != DT_NULL_INDEX);

		_nodes[index].aabb = math::envelop(_nodes[child1].aabb, _nodes[child2].aabb);
		_nodes[index].categoryBits = _nodes[child1].categoryBits | _nodes[child2].categoryBits;
		_nodes[index].height = 1 + std::max(_nodes[child1].height, _nodes[child2].height );
		_nodes[index].flags |= ( _nodes[child1].flags | _nodes[child2].flags ) & DynamicTreeNode<UserT>::Flag::enlargedNode;

		if(shouldRotate)
			rotateNodes(index);

		index = _nodes[index].parent;
	}
}

template<typename UserT>
void DynamicTree<UserT>::removeLeaf(TreeNodeIndex leaf)
{
	if(leaf == _root)
	{
		_root = DT_NULL_INDEX;
		return;
	}

	auto parent = _nodes[leaf].parent;
	auto grandParent = _nodes[parent].parent;
	TreeNodeIndex sibling;
	if(_nodes[parent].children.child1 == leaf)
		sibling = _nodes[parent].children.child2;
	else
		sibling = _nodes[parent].children.child1;

	if(grandParent != DT_NULL_INDEX)
	{
		// Destroy parent and connect sibling to grandParent.
		if(_nodes[grandParent].children.child1 == parent )
			_nodes[grandParent].children.child1 = sibling;
		else
			_nodes[grandParent].children.child2 = sibling;
		_nodes[sibling].parent = grandParent;
		freeNode(parent);

		// Adjust ancestor bounds.
		auto index = grandParent;
		while(index != DT_NULL_INDEX)
		{
			auto &node = _nodes[index];
			auto &child1 = _nodes[node.children.child1];
			auto &child2 = _nodes[node.children.child2];

			// Fast union using SSE
			//__m128 aabb1 = _mm_load_ps(&child1->aabb.lowerBound.x);
			//__m128 aabb2 = _mm_load_ps(&child2->aabb.lowerBound.x);
			//__m128 lower = _mm_min_ps(aabb1, aabb2);
			//__m128 upper = _mm_max_ps(aabb1, aabb2);
			//__m128 aabb = _mm_shuffle_ps(lower, upper, _MM_SHUFFLE(3, 2, 1, 0));
			//_mm_store_ps(&node->aabb.lowerBound.x, aabb);

			node.aabb = math::envelop(child1.aabb, child2.aabb);
			node.categoryBits = child1.categoryBits | child2.categoryBits;
			node.height = 1 + std::max(child1.height, child2.height );

			index = node.parent;
		}
	}
	else
	{
		_root = sibling;
		_nodes[sibling].parent = DT_NULL_INDEX;
		freeNode(parent);
	}
}

template<typename UserT>
TreeNodeIndex DynamicTree<UserT>::findBestSibling(bounds::AABB box)
{
	const auto centerD = box.center();
	const auto areaD = box.surfaceArea();

	const auto rootIndex = _root;

	auto rootBox = _nodes[rootIndex].aabb;

	// Area of current node
	auto areaBase = rootBox.surfaceArea();

	// Area of inflated node
	float directCost = math::envelop(rootBox, box).surfaceArea();
	float inheritedCost = 0;

	auto bestSibling = rootIndex;
	auto bestCost = directCost;

	// Descend the tree from root, following a single greedy path.
	auto index = rootIndex;
	while(not _nodes[index].isLeaf())
	{
		auto child1 = _nodes[index].children.child1;
		auto child2 = _nodes[index].children.child2;

		// Cost of creating a new parent for this node and the new leaf
		float cost = directCost + inheritedCost;

		// Sometimes there are multiple identical costs within tolerance.
		// This breaks the ties using the centroid distance.
		if(cost < bestCost)
		{
			bestSibling = index;
			bestCost = cost;
		}

		// Inheritance cost seen by children
		inheritedCost += directCost - areaBase;

		bool leaf1 = _nodes[child1].isLeaf();
		bool leaf2 = _nodes[child2].isLeaf();

		// Cost of descending into child 1
		float lowerCost1 = std::numeric_limits<float>::max();
		bounds::AABB box1 = _nodes[child1].aabb;
		float directCost1 = math::envelop(box1, box).surfaceArea();
		float area1 = 0;
		if(leaf1)
		{
			// Child 1 is a leaf
			// Cost of creating new node and increasing area of node P
			float cost1 = directCost1 + inheritedCost;

			// Need this here due to while condition above
			if(cost1 < bestCost)
			{
				bestSibling = child1;
				bestCost = cost1;
			}
		}
		else
		{
			// Child 1 is an internal node
			area1 = box1.surfaceArea();

			// Lower bound cost of inserting under child 1.
			lowerCost1 = inheritedCost + directCost1 + std::min(areaD - area1, 0.0f);
		}

		// Cost of descending into child 2
		float lowerCost2 = std::numeric_limits<float>::min();
		bounds::AABB box2 = _nodes[child2].aabb;
		float directCost2 = math::envelop( box2, box).surfaceArea();
		float area2 = 0.0f;
		if(leaf2)
		{
			// Child 2 is a leaf
			// Cost of creating new node and increasing area of node P
			float cost2 = directCost2 + inheritedCost;

			// Need this here due to while condition above
			if(cost2 < bestCost)
			{
				bestSibling = child2;
				bestCost = cost2;
			}
		}
		else
		{
			// Child 2 is an internal node
			area2 = box2.surfaceArea();

			// Lower bound cost of inserting under child 2. This is not the cost
			// of child 2, it is the best we can hope for under child 2.
			lowerCost2 = inheritedCost + directCost2 + std::min(areaD - area2, 0.f);
		}

		if(leaf1 and leaf2)
			break;

		// Can the cost possibly be decreased?
		if(bestCost <= lowerCost1 and bestCost <= lowerCost2)
			break;

		if(lowerCost1 == lowerCost2 and not leaf1)
		{
			// No clear choice based on lower bound surface area. This can happen when both
			// children fully contain D. Fall back to node distance.
			auto d1 = box1.center() - centerD;
			auto d2 = box2.center() - centerD;
			lowerCost1 = glm::length2(d1);
			lowerCost2 = glm::length2(d2);
		}

		// Descend
		if(lowerCost1 < lowerCost2 and not leaf1)
		{
			index = child1;
			areaBase = area1;
			directCost = directCost1;
		}
		else
		{
			index = child2;
			areaBase = area2;
			directCost = directCost2;
		}

		assert(not _nodes[index].isLeaf());
	}

	return bestSibling;
}

template<typename UserT>
void DynamicTree<UserT>::rotateNodes(uint32_t iA)
{
	assert(iA != DT_NULL_INDEX);

	enum class RotateType
	{
		None,
		BF,
		BG,
		CD,
		CE
	};

	auto &A = _nodes[iA];
	if(A.isLeaf())
		return;

	auto iB = A.children.child1;
	auto iC = A.children.child2;
	assert(0 <= iB and iB < _nodeCapacity);
	assert(0 <= iC and iC < _nodeCapacity);

	auto &B = _nodes[iB];
	auto &C = _nodes[iC];

	bool isLeafB = B.isLeaf();
	bool isLeafC = C.isLeaf();

	if(isLeafB and not isLeafC)
	{
		auto iF = C.children.child1;
		auto iG = C.children.child2;
		auto &F = _nodes[iF];
		auto &G = _nodes[iG];
		assert(0 <= iF and iF < _nodeCapacity);
		assert(0 <= iG and iG < _nodeCapacity);

			   // Base cost
		float costBase = C.aabb.surfaceArea();

			   // Cost of swapping B and F
		auto aabbBG = math::envelop(B.aabb, C.aabb);
		float costBF = aabbBG.surfaceArea();

			   // Cost of swapping B and G
		auto aabbBF = math::envelop(B.aabb, F.aabb);
		float costBG = aabbBF.surfaceArea();

		if(costBase < costBF and costBase < costBG)
		{
			// Rotation does not improve cost
			return;
		}

		if(costBF < costBG)
		{
			// Swap B and F
			A.children.child1 = iF;
			C.children.child1 = iB;

			B.parent = iC;
			F.parent = iA;

			C.aabb = aabbBG;

			C.height = 1 + std::max(B.height, G.height);
			A.height = 1 + std::max(C.height, F.height);
			C.categoryBits = B.categoryBits | G.categoryBits;
			A.categoryBits = C.categoryBits | F.categoryBits;
			C.flags |= (B.flags | G.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
			A.flags |= (C.flags | F.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
		}
		else
		{
			// Swap B and G
			A.children.child1 = iG;
			C.children.child2 = iB;

			B.parent = iC;
			G.parent = iA;

			C.aabb = aabbBF;

			C.height = 1 + std::max(B.height, F.height);
			A.height = 1 + std::max(C.height, G.height);
			C.categoryBits = B.categoryBits | F.categoryBits;
			A.categoryBits = C.categoryBits | G.categoryBits;
			C.flags |= (B.flags | F.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
			A.flags |= (C.flags | G.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
		}
	}
	else if(isLeafC and not isLeafB)
	{
		// C is a leaf and B is internal

		auto iD = B.children.child1;
		auto iE = B.children.child2;
		auto &D = _nodes[iD];
		auto &E = _nodes[iE];
		assert(0 <= iD and iD < _nodeCapacity);
		assert(0 <= iE and iE < _nodeCapacity);

			   // Base cost
		float costBase = B.aabb.surfaceArea();

			   // Cost of swapping C and D
		auto aabbCE = math::envelop(C.aabb, E.aabb);
		float costCD = aabbCE.surfaceArea();

			   // Cost of swapping C and E
		auto aabbCD = math::envelop(C.aabb, D.aabb);
		float costCE = aabbCD.surfaceArea();

		if(costBase < costCD and costBase < costCE)
		{
			// Rotation does not improve cost
			return;
		}

		if(costCD < costCE)
		{
			// Swap C and D
			A.children.child2 = iD;
			B.children.child1 = iC;

			C.parent = iB;
			D.parent = iA;

			B.aabb = aabbCE;

			B.height = 1 + std::max(C.height, E.height);
			A.height = 1 + std::max(B.height, D.height);
			B.categoryBits = C.categoryBits | E.categoryBits;
			A.categoryBits = B.categoryBits | D.categoryBits;
			B.flags |= (C.flags | E.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
			A.flags |= (B.flags | D.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
		}
		else
		{
			// Swap C and E
			A.children.child2 = iE;
			B.children.child2 = iC;

			C.parent = iB;
			E.parent = iA;

			B.aabb = aabbCD;

			B.height = 1 + std::max(C.height, D.height);
			A.height = 1 + std::max(B.height, E.height);
			B.categoryBits = C.categoryBits | D.categoryBits;
			A.categoryBits = B.categoryBits | E.categoryBits;
			B.flags |= (C.flags | D.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
			A.flags |= (B.flags | E.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
		}
	}
	else if(not isLeafB and not isLeafC)
	{
		// All grand children exist so there are many options for rotation
		auto iD = B.children.child1;
		auto iE = B.children.child2;
		auto iF = C.children.child1;
		auto iG = C.children.child2;

		assert(0 <= iD and iD < _nodeCapacity);
		assert(0 <= iE and iE < _nodeCapacity);
		assert(0 <= iF and iF < _nodeCapacity);
		assert(0 <= iG and iG < _nodeCapacity);

		auto &D = _nodes[iD];
		auto &E = _nodes[iE];
		auto &F = _nodes[iF];
		auto &G = _nodes[iG];

			   // Base cost
		float areaB = B.aabb.surfaceArea();
		float areaC = C.aabb.surfaceArea();
		float costBase = areaB + areaC;
		auto bestRotation = RotateType::None;
		float bestCost = costBase;

			   // Cost of swapping B and F
		auto aabbBG = math::envelop(B.aabb, G.aabb);
		float costBF = areaB + aabbBG.surfaceArea();
		if(costBF < bestCost)
		{
			bestRotation = RotateType::BF;
			bestCost = costBF;
		}

			   // Cost of swapping B and G
		auto aabbBF = math::envelop(B.aabb, F.aabb);
		float costBG = areaB + aabbBF.surfaceArea();
		if(costBG < bestCost)
		{
			bestRotation = RotateType::BG;
			bestCost = costBG;
		}

			   // Cost of swapping C and D
		auto aabbCE = math::envelop(C.aabb, E.aabb);
		float costCD = areaC + aabbCE.surfaceArea();
		if(costCD < bestCost)
		{
			bestRotation = RotateType::CD;
			bestCost = costCD;
		}

			   // Cost of swapping C and E
		auto aabbCD = math::envelop(C.aabb, D.aabb);
		float costCE = areaC + aabbCD.surfaceArea();
		if(costCE < bestCost)
		{
			bestRotation = RotateType::CE;
			// bestCost = costCE;
		}

		switch(bestRotation)
		{
		case RotateType::None:
			break;

		case RotateType::BF:
			A.children.child1 = iF;
			C.children.child1 = iB;

			B.parent = iC;
			F.parent = iA;

			C.aabb = aabbBG;

			C.height = 1 + std::max(B.height, G.height);
			A.height = 1 + std::max(C.height, F.height);
			C.categoryBits = B.categoryBits | G.categoryBits;
			A.categoryBits = C.categoryBits | F.categoryBits;
			C.flags |= (B.flags | G.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
			A.flags |= (C.flags | F.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
			break;

		case RotateType::BG:
			A.children.child1 = iG;
			C.children.child2 = iB;

			B.parent = iC;
			G.parent = iA;

			C.aabb = aabbBF;

			C.height = 1 + std::max(B.height, F.height);
			A.height = 1 + std::max(C.height, G.height);
			C.categoryBits = B.categoryBits | F.categoryBits;
			A.categoryBits = C.categoryBits | G.categoryBits;
			C.flags |= (B.flags | F.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
			A.flags |= (C.flags | G.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
			break;

		case RotateType::CD:
			A.children.child2 = iD;
			B.children.child1 = iC;

			C.parent = iB;
			D.parent = iA;

			B.aabb = aabbCE;

			B.height = 1 + std::max(C.height, E.height);
			A.height = 1 + std::max(B.height, D.height);
			B.categoryBits = C.categoryBits | E.categoryBits;
			A.categoryBits = B.categoryBits | D.categoryBits;
			B.flags |= (C.flags | E.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
			A.flags |= (B.flags | D.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
			break;

		case RotateType::CE:
			A.children.child2 = iE;
			B.children.child2 = iC;

			C.parent = iB;
			E.parent = iA;

			B.aabb = aabbCD;

			B.height = 1 + std::max(C.height, D.height);
			A.height = 1 + std::max(B.height, E.height);
			B.categoryBits = C.categoryBits | D.categoryBits;
			A.categoryBits = B.categoryBits | E.categoryBits;
			B.flags |= (C.flags | D.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
			A.flags |= (B.flags | E.flags) & DynamicTreeNode<UserT>::Flag::enlargedNode;
			break;

		default:
			assert(false);
			break;
		}
	}
}

template<typename UserT>
UserT DynamicTree<UserT>::proxyUserData(TreeProxyID id) const
{
	return _nodes[id].userData;
}

template<typename UserT>
bounds::AABB DynamicTree<UserT>::proxyAABB(TreeProxyID id) const
{
	return _nodes[id].aabb;
}

template<typename UserT>
size_t DynamicTree<UserT>::byteSize() const
{
	auto size = sizeof(decltype(this)) + sizeof(decltype(_nodes)::value_type) * _nodes.capacity();
	/* TODO: rebuild size_t(_rebuildCapacity) *  (sizeof(uint32_t) + sizeof(bounds::AABB) + sizeof(glm::vec3) + sizeof(uint32_t)); */

	return size;
}

template<typename UserT>
float DynamicTree<UserT>::nodeDistanceSq(glm::vec3 point, const DynamicTreeNode<UserT> &node) const
{
	const auto r = point - glm::clamp(point, node.aabb.min(), node.aabb.max());
	return glm::dot(r, r);
}


#define DT_TREE_STACK_SIZE 1024

template<typename UserT>
DynamicTreeStats DynamicTree<UserT>::query(bounds::AABB aabb, uint64_t maskBits, bool requireAllBits, QueryCb callback) const
{
	DynamicTreeStats stats {
		.nodeVisits = 0,
		.leafVisits = 0,
	};

	if(not _nodeCount)
		return stats;

	TreeNodeIndex stack[DT_TREE_STACK_SIZE];
	uint32_t stackCount { 0 };
	stack[stackCount++] = _root;

	while(stackCount > 0)
	{
		auto nodeId = stack[--stackCount];
		if(nodeId == DT_NULL_INDEX)
		{
			// todo huh?
			assert(false);
			continue;
		}

		const auto &node = _nodes[nodeId];
		++stats.nodeVisits;

		// Assuming branch prediction deals with requireAllBits well
		const auto bitMatch = requireAllBits? (node.categoryBits & maskBits) == maskBits: (node.categoryBits & maskBits);
		if(bitMatch and intersect::check(node.aabb, aabb))
		{
			if(node.isLeaf())
			{
				// callback to user code with proxy id
				const bool proceed = callback(nodeId, node.userData);
				++stats.leafVisits;

				if(not proceed)
					return stats;
			}
			else
			{
				assert(stackCount < DT_TREE_STACK_SIZE - 1);
				if(stackCount < DT_TREE_STACK_SIZE - 1)
				{
					stack[stackCount++] = node.children.child1;
					stack[stackCount++] = node.children.child2;
				}
			}
		}
	}

	return stats;
}

template<typename UserT>
DynamicTreeStats DynamicTree<UserT>::queryClosest(glm::vec3 point, uint64_t maskBits, bool requireAllBits, QueryClosestCb callback, float &minDistanceSqr) const
{
	DynamicTreeStats stats {
		.nodeVisits = 0,
		.leafVisits = 0,
	};

	if(not _nodeCount)
		return stats;

	float minSqr = minDistanceSqr;

	struct ClosestItem
	{
		TreeNodeIndex nodeIndex;
		float distanceToNodeSqr;
	};
	ClosestItem stack[DT_TREE_STACK_SIZE];
	auto stackCount = 0u;

	float rootDistanceSqr = nodeDistanceSq(point, _nodes[_root]);
	stack[stackCount++] = {
		.nodeIndex = _root,
		.distanceToNodeSqr = rootDistanceSqr,
	};

	while( stackCount > 0)
	{
		auto item = stack[--stackCount];
		const auto &node = _nodes[item.nodeIndex];
		++stats.nodeVisits;

		const auto bitMatch = requireAllBits? (node.categoryBits & maskBits) == maskBits: (node.categoryBits & maskBits);
		if(bitMatch)
		{
			if(item.distanceToNodeSqr < minSqr)
			{
				if(node.isLeaf())
				{
					// callback to user code with minimum distance squared so far and proxy id
					const float dd = callback(minSqr, item.nodeIndex, node.userData);

					if(dd < minSqr)
						minSqr = dd;

					++stats.leafVisits;
				}
				else
				{
					assert(stackCount < DT_TREE_STACK_SIZE - 1);
					if(stackCount < DT_TREE_STACK_SIZE - 1)
					{
						const auto child1 = node.children.child1;
						const auto child2 = node.children.child2;

						// Store the distance to node in the stack instead of recomputing after pop
						ClosestItem item1 {
							.nodeIndex = child1,
							.distanceToNodeSqr = nodeDistanceSq( point, _nodes[child1]),
						};

						ClosestItem item2 {
							.nodeIndex = child2,
							.distanceToNodeSqr = nodeDistanceSq(point, _nodes[child2]),
						};

						// Ensure we iterate the closest child first as we pop off the stack
						if(item2.distanceToNodeSqr < item1.distanceToNodeSqr)
						{
							stack[stackCount++] = item1;
							stack[stackCount++] = item2;
						}
						else
						{
							stack[stackCount++] = item2;
							stack[stackCount++] = item1;
						}
					}
				}
			}
		}
	}

	minDistanceSqr = minSqr;

	return stats;
}


static inline glm::vec3 modifiedCross(glm::vec3 a, glm::vec3 b)
{
	// like cross(a, b) but sums instead of differences
	// TODO: this function needs a better name...
	return { a.y * b.z + a.z * b.y, a.z * b.x + a.x * b.z, a.x * b.y + a.y * b.x };
}

static inline bool testBoundsRayOverlap(glm::vec3 nodeMin, glm::vec3 nodeMax, glm::vec3 rayStart, glm::vec3 rayDelta)
{
	// Setup node
	// b3V32 nodeCenter = b3MulV( b3_halfV, b3AddV( nodeMin, nodeMax ) );
	auto nodeCenter = 0.5f * (nodeMin + nodeMax);
	// b3V32 nodeExtent = b3SubV( nodeMax, nodeCenter );
	auto nodeExtent = nodeMax - nodeCenter;

	// Setup ray
	// rayStart = b3SubV( rayStart, nodeCenter );
	rayStart -= nodeCenter;

	// SAT: Edge separation
	// b3V32 edgeSeparation = b3SubV( b3AbsV( b3CrossV( rayDelta, rayStart ) ), b3ModifiedCrossV( b3AbsV( rayDelta ), nodeExtent ) );
	auto edgeSeparation = glm::abs(glm::cross(rayDelta, rayStart)) - modifiedCross(glm::abs(rayDelta), nodeExtent);
	// return b3AllLessEq3V( edgeSeparation, b3_zeroV );
	return glm::all(glm::lessThanEqual(edgeSeparation, glm::zero<glm::vec3>()));
}


template<typename UserT>
DynamicTreeStats DynamicTree<UserT>::castRay(const TreeRayCastInput &input, uint64_t maskBits, bool requireAllBits, CastRayCb callback) const
{
	DynamicTreeStats stats {
		.nodeVisits = 0,
		.leafVisits = 0,
	};

	if(not _nodeCount)
		return stats;

	auto p1 = input.origin;
	auto d = input.translation;

	float maxFraction = input.maxFraction;

	// b3Vec3 p2 = b3MulAdd( p1, maxFraction, d );
	auto p2 = p1 + d * maxFraction;

	// Build a bounding box for the segment.
	auto segmentAABB = bounds::AABB{ glm::min(p1, p2), glm::max(p1, p2) };

	TreeNodeIndex stack[DT_TREE_STACK_SIZE];
	auto stackCount = 0u;
	stack[stackCount++] = _root;

	auto subInput = input;

	while(stackCount > 0)
	{
		auto nodeId = stack[--stackCount];
		if(nodeId == DT_NULL_INDEX)
		{
			// todo is this possible?
			assert(false);
			continue;
		}

		const auto &node = _nodes[nodeId];
		++stats.nodeVisits;

		const auto nodeAABB = node.aabb;

		const auto bitMatch = requireAllBits? (node.categoryBits & maskBits ) == maskBits: (node.categoryBits & maskBits);
		if(not bitMatch or not intersect::check(nodeAABB, segmentAABB))
			continue;

		const auto lower = nodeAABB.min();
		const auto upper = nodeAABB.max();

		bool edgeOverlap = testBoundsRayOverlap(lower, upper, p1, d);
		if(not edgeOverlap)
			continue;

		if(node.isLeaf())
		{
			subInput.maxFraction = maxFraction;

			const float value = callback(subInput, nodeId, node.userData);
			++stats.leafVisits;

			// The user may return -1 to indicate this shape should be skipped

			if(value == 0)  // The client has terminated the ray cast.
				return stats;

			if(value > 0 and value <= maxFraction)
			{
				// Update segment bounding box.
				maxFraction = value;
				// p2 = b3MulAdd( p1, maxFraction, d );
				p2 = p1 + d * maxFraction;
				segmentAABB.min() = glm::min(p1, p2);
				segmentAABB.max() = glm::max(p1, p2);
			}
		}
		else
		{
			assert(stackCount < DT_TREE_STACK_SIZE - 1);
			if(stackCount < DT_TREE_STACK_SIZE - 1)
			{
				const auto c1 = _nodes[node.children.child1].aabb.center();
				const auto c2 = _nodes[node.children.child2].aabb.center();
				if(glm::distance2(c1, p1) < glm::distance2(c2, p1))
				{
					stack[stackCount++] = node.children.child2;
					stack[stackCount++] = node.children.child1;
				}
				else
				{
					stack[stackCount++] = node.children.child1;
					stack[stackCount++] = node.children.child2;
				}
			}
		}
	}

	return stats;
}


template<typename UserT>
DynamicTreeStats DynamicTree<UserT>::castBox(const TreeBoxCastInput &input, uint64_t maskBits, bool requireAllBits, CastBoxCb callback) const
{
	DynamicTreeStats stats {
		.nodeVisits = 0,
		.leafVisits = 0,
	};

	if(not _nodeCount)
		return stats;

	// The caller folds the shape radius and the world origin into the box
	auto originAABB = input.box;

	glm::vec3 p1 = originAABB.center();
	glm::vec3 extension { originAABB.width(), originAABB.height(), originAABB.depth() };

	auto d = input.translation;

	float maxFraction = input.maxFraction;

	// Build total box for the cast
	glm::vec3 t = input.translation * maxFraction;
	bounds::AABB totalAABB = {
		glm::min(originAABB.min(), originAABB.min() + t),
		glm::max(originAABB.max(), originAABB.max() + t),
	};

	auto subInput = input;

	TreeNodeIndex stack[DT_TREE_STACK_SIZE];
	auto stackCount = 0u;
	stack[stackCount++] = _root;

	while(stackCount > 0)
	{
		auto nodeId = stack[--stackCount];
		if(nodeId == DT_NULL_INDEX)
		{
			assert(false);
			continue;
		}

		const auto &node = _nodes[nodeId];
		++stats.nodeVisits;

		const auto bitMatch = requireAllBits? (node.categoryBits & maskBits) == maskBits: (node.categoryBits & maskBits);
		if(not bitMatch or not intersect::check(node.aabb, totalAABB))
			continue;

		// radius extension is added to the node in this case
		const auto lower = node.aabb.min() - extension;
		const auto upper = node.aabb.max() + extension;
		bool edgeOverlap = testBoundsRayOverlap(lower, upper, p1, d);
		if(not edgeOverlap)
			continue;

		if(node.isLeaf())
		{
			subInput.maxFraction = maxFraction;

			const float value = callback(subInput, nodeId, node.userData);
			++stats.leafVisits;

			if(value == 0)
			{
				// The client has terminated the cast.
				return stats;
			}

			if(value > 0 and value < maxFraction)
			{
				maxFraction = value;
				t = input.translation * maxFraction;
				totalAABB.min() = glm::min(originAABB.min(), originAABB.min() + t);
				totalAABB.max() = glm::max(originAABB.max(), originAABB.max() + t);
			}
		}
		else
		{
			assert(stackCount < DT_TREE_STACK_SIZE - 1);
			if(stackCount < DT_TREE_STACK_SIZE - 1)
			{
				const auto c1 = _nodes[node.children.child1].aabb.center();
				const auto c2 = _nodes[node.children.child2].aabb.center();
				if(glm::distance2(c1, p1) < glm::distance2(c2, p1))
				{
					stack[stackCount++] = node.children.child2;
					stack[stackCount++] = node.children.child1;
				}
				else
				{
					stack[stackCount++] = node.children.child1;
					stack[stackCount++] = node.children.child2;
				}
			}
		}
	}

	return stats;
}


#if defined(TREE_VALIDATION)

// Compute the height of a sub-tree.
template<typename UserT>
int b3DynamicTree<UserT>::computeHeightRecurse(TreeNodeIndex nodeId) const
{
	assert( 0 <= nodeId and nodeId < _nodeCapacity);
	auto &node = _nodes[nodeId];

	if(node.isLeaf())
		return 0;

	auto height1 = computeHeightRecurse(node.children.child1);
	auto height2 = computeHeightRecurse(node.children.child2);
	return 1 + std::max(height1, height2);
}

template<typename UserT>
int b3DynamicTree<UserT>::computeHeight() const
{
	return computeHeightRecurse(_root);
}

template<typename UserT>
void b3DynamicTree<UserT>::validateStructure(TreeNodeIndex index) const
{
	if(index == B3_NULL_INDEX)
		return;

	if(index == _root)
		assert(_nodes[index].parent == B3_NULL_INDEX);

	const auto &node = _nodes[index];

	assert(node.flags == 0 or (node.flags & b3_allocatedNode) != 0);

	if(node.isLeaf())
	{
		assert(node.height == 0);
		return;
	}

	auto child1 = node.children.child1;
	auto child2 = node.children.child2;

	assert(0 <= child1 and child1 < _nodeCapacity);
	assert(0 <= child2 and child2 < _nodeCapacity);

	assert(_nodes[child1].parent == index);
	assert(_nodes[child2].parent == index);

	if((_nodes[child1].flags | _nodes[child2].flags) & b3_enlargedNode)
		assert(node.flags & b3_enlargedNode);

	validateStructure(child1);
	validateStructure(child2);
}

template<typename UserT>
void b3DynamicTree<UserT>::validateMetrics(TreeNodeIndex index) const
{
	if(index == B3_NULL_INDEX)
		return;

	const auto &node = _nodes[index];

	assert(math::valid(node.aabb));

	if(node.isLeaf())
	{
		assert(node.height == 0);
		return;
	}

	auto child1 = node.children.child1;
	auto child2 = node.children.child2;

	assert(0 <= child1 and child1 < _nodeCapacity);
	assert(0 <= child2 and child2 < _nodeCapacity);

	auto height1 = _nodes[child1].height;
	auto height2 = _nodes[child2].height;
	auto height = 1 + std::max(height1, height2);
	assert(node.height == height);

		   // b3AABB aabb = b3AABB_Union(tree->nodes[child1].aabb, tree->nodes[child2].aabb);

	assert(intersect::check(node.aabb, _nodes[child1].aabb));
	assert(intersect::check(node.aabb, _nodes[child2].aabb));

		   // assert(aabb.lowerBound.x == node.aabb.lowerBound.x);
		   // assert(aabb.lowerBound.y == node.aabb.lowerBound.y);
		   // assert(aabb.upperBound.x == node.aabb.upperBound.x);
		   // assert(aabb.upperBound.y == node.aabb.upperBound.y);

	auto categoryBits = _nodes[child1].categoryBits | _nodes[child2].categoryBits;
	assert(node.categoryBits == categoryBits);

	validateMetrics(child1);
	validateMetrics(child2);
}

template<typename UserT>
void b3DynamicTree<UserT>::validate()
{
	if(_root == B3_NULL_INDEX)
		return;

	validateStructure(_root);
	validateMetrics(_root);

	auto freeCount = 0u;
	auto freeIndex = _freeList;
	while(freeIndex != B3_NULL_INDEX )
	{
		assert(0 <= freeIndex and freeIndex < _nodeCapacity);
		freeIndex = _nodes[freeIndex].next;
		++freeCount;
	}

	auto height = this->height();
	auto computedHeight = computeHeight();
	assert(height == computedHeight);

	assert(_nodeCount + freeCount == _nodeCapacity);
}

template<typename UserT>
void b3DynamicTree<UserT>::validateNoGrown() const
{
	for(auto i = 0u; i < _nodeCapacity; ++i)
	{
		const auto &node = _nodes[i];
		if(node.flags & b3_allocatedNode)
			assert((node.flags & b3_enlargedNode) == 0);
	}
}

#endif // TREE_VALIDATION

} // RGL