// SPDX-FileCopyrightText: 2025 Erin Catto
// SPDX-License-Identifier: MIT

#include "dynamic_tree.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

#include <cstring>

namespace RGL
{

#define B3_TREE_STACK_SIZE 1024

static b3TreeNode b3_defaultTreeNode = {
	.aabb = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
	.categoryBits = B3_DEFAULT_CATEGORY_BITS,
	.children = {
		.child1 = B3_NULL_INDEX,
		.child2 = B3_NULL_INDEX,
	},
	.parent = B3_NULL_INDEX,
	.height = 0,
	.flags = b3_allocatedNode,
};

b3DynamicTree::b3DynamicTree(uint32_t proxyCapacity) :
	_version(B3_DYNAMIC_TREE_VERSION),
	_root(B3_NULL_INDEX),
	_nodeCount(0),
	_nodeCapacity(0),
	_proxyCount(0),
	_freeList(0)
	// TODO: rebuild rebuildCapacity(0)
{
	auto capacity = std::max(proxyCapacity, 16u);

	_nodeCapacity = 2u * capacity - 1u;
	_nodes.resize(_nodeCapacity);

	std::memset(_nodes.data(), 0, _nodes.size() * sizeof(b3TreeNode));

	// Build a linked list for the free list.
	// todo use a bump allocator until the capacity is consumed (see b3PoolAllocator)
	for(auto idx = 0u; idx < _nodeCapacity - 2; ++idx)
		_nodes[idx].next = idx + 1u;
	_nodes.back().next = B3_NULL_INDEX;
}

b3DynamicTree::~b3DynamicTree()
{
}

// Allocate a node from the pool. Grow the pool if necessary.
b3DynamicTree::NodeIndex b3DynamicTree::allocateNode()
{
	// Expand the node pool as needed.
	if(_freeList == B3_NULL_INDEX )
	{
		assert(_nodeCount == _nodeCapacity);

		// The free list is empty. Rebuild a bigger pool.
		// b3TreeNode* oldNodes = tree->nodes;
		_nodeCapacity += _nodeCapacity >> 1;
		_nodes.resize(_nodeCapacity);
		// zero the new nodes
		std::memset(&_nodes[_nodeCount], 0, (_nodeCapacity - _nodeCount) * sizeof(decltype(_nodes)::value_type));

		// Build a linked list for the free list. The parent pointer becomes the "next" pointer.
		// todo avoid building freelist?
		for(auto idx = _nodeCount; idx < _nodeCapacity - 1; ++idx )
			_nodes[idx].next = idx + 1;
		_nodes.back().next = B3_NULL_INDEX;

		_freeList = _nodeCount;
	}

	// Peel a node off the free list.
	const auto nodeIndex = _freeList;
	auto &node = _nodes[size_t(nodeIndex)];
	_freeList = node.next;
	node = b3_defaultTreeNode;
	++_nodeCount;

	return nodeIndex;
}

// Return a node to the pool.
void b3DynamicTree::freeNode(uint32_t nodeId)
{
	assert( 0 <= nodeId and nodeId < _nodeCapacity );
	assert( 0 < _nodeCount );
	_nodes[nodeId].next = _freeList;
	_nodes[nodeId].flags = 0;
	_freeList = nodeId;
	--_nodeCount;
}

// Greedy algorithm for sibling selection using the SAH
// We have three nodes A-(B,C) and want to add a leaf D, there are three choices.
// 1: make a new parent for A and D : E-(A-(B,C), D)
// 2: associate D with B
//   a: B is a leaf : A-(E-(B,D), C)
//   b: B is an internal node: A-(B{D},C)
// 3: associate D with C
//   a: C is a leaf : A-(B, E-(C,D))
//   b: C is an internal node: A-(B, C{D})
// All of these have a clear cost except when B or C is an internal node. Hence we need to be greedy.

// The cost for cases 1, 2a, and 3a can be computed using the sibling cost formula.
// cost of sibling H = area(union(H, D)) + increased area of ancestors

// Suppose B (or C) is an internal node, then the lowest cost would be one of two cases:
// case1: D becomes a sibling of B
// case2: D becomes a descendant of B along with a new internal node of area(D).
b3DynamicTree::NodeIndex b3DynamicTree::findBestSibling(bounds::AABB box)
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
	while (not _nodes[index].isLeaf())
	{
		auto child1 = _nodes[index].children.child1;
		auto child2 = _nodes[index].children.child2;

		// Cost of creating a new parent for this node and the new leaf
		float cost = directCost + inheritedCost;

		// Sometimes there are multiple identical costs within tolerance.
		// This breaks the ties using the centroid distance.
		if ( cost < bestCost )
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
			if ( cost1 < bestCost )
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
			lowerCost2 = inheritedCost + directCost2 + std::min(areaD - area2, 0.0f);
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

enum b3RotateType
{
	b3_rotateNone,
	b3_rotateBF,
	b3_rotateBG,
	b3_rotateCD,
	b3_rotateCE
};

// Perform a left or right rotation if node A is imbalanced.
// Returns the new root index.
void b3DynamicTree::rotateNodes(uint32_t iA)
{
	assert(iA != B3_NULL_INDEX);

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
			C.flags |= (B.flags | G.flags) & b3_enlargedNode;
			A.flags |= (C.flags | F.flags) & b3_enlargedNode;
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
			C.flags |= (B.flags | F.flags) & b3_enlargedNode;
			A.flags |= (C.flags | G.flags) & b3_enlargedNode;
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
			B.flags |= (C.flags | E.flags) & b3_enlargedNode;
			A.flags |= (B.flags | D.flags) & b3_enlargedNode;
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
			B.flags |= (C.flags | D.flags) & b3_enlargedNode;
			A.flags |= (B.flags | E.flags) & b3_enlargedNode;
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
		auto bestRotation = b3_rotateNone;
		float bestCost = costBase;

		// Cost of swapping B and F
		auto aabbBG = math::envelop(B.aabb, G.aabb);
		float costBF = areaB + aabbBG.surfaceArea();
		if(costBF < bestCost)
		{
			bestRotation = b3_rotateBF;
			bestCost = costBF;
		}

		// Cost of swapping B and G
		auto aabbBF = math::envelop(B.aabb, F.aabb);
		float costBG = areaB + aabbBF.surfaceArea();
		if(costBG < bestCost)
		{
			bestRotation = b3_rotateBG;
			bestCost = costBG;
		}

		// Cost of swapping C and D
		auto aabbCE = math::envelop(C.aabb, E.aabb);
		float costCD = areaC + aabbCE.surfaceArea();
		if(costCD < bestCost)
		{
			bestRotation = b3_rotateCD;
			bestCost = costCD;
		}

		// Cost of swapping C and E
		auto aabbCD = math::envelop(C.aabb, D.aabb);
		float costCE = areaC + aabbCD.surfaceArea();
		if(costCE < bestCost)
		{
			bestRotation = b3_rotateCE;
			// bestCost = costCE;
		}

		switch(bestRotation)
		{
			case b3_rotateNone:
				break;

			case b3_rotateBF:
				A.children.child1 = iF;
				C.children.child1 = iB;

				B.parent = iC;
				F.parent = iA;

				C.aabb = aabbBG;

				C.height = 1 + std::max(B.height, G.height);
				A.height = 1 + std::max(C.height, F.height);
				C.categoryBits = B.categoryBits | G.categoryBits;
				A.categoryBits = C.categoryBits | F.categoryBits;
				C.flags |= (B.flags | G.flags) & b3_enlargedNode;
				A.flags |= (C.flags | F.flags) & b3_enlargedNode;
				break;

			case b3_rotateBG:
				A.children.child1 = iG;
				C.children.child2 = iB;

				B.parent = iC;
				G.parent = iA;

				C.aabb = aabbBF;

				C.height = 1 + std::max(B.height, F.height);
				A.height = 1 + std::max(C.height, G.height);
				C.categoryBits = B.categoryBits | F.categoryBits;
				A.categoryBits = C.categoryBits | G.categoryBits;
				C.flags |= (B.flags | F.flags) & b3_enlargedNode;
				A.flags |= (C.flags | G.flags) & b3_enlargedNode;
				break;

			case b3_rotateCD:
				A.children.child2 = iD;
				B.children.child1 = iC;

				C.parent = iB;
				D.parent = iA;

				B.aabb = aabbCE;

				B.height = 1 + std::max(C.height, E.height);
				A.height = 1 + std::max(B.height, D.height);
				B.categoryBits = C.categoryBits | E.categoryBits;
				A.categoryBits = B.categoryBits | D.categoryBits;
				B.flags |= (C.flags | E.flags) & b3_enlargedNode;
				A.flags |= (B.flags | D.flags) & b3_enlargedNode;
				break;

			case b3_rotateCE:
				A.children.child2 = iE;
				B.children.child2 = iC;

				C.parent = iB;
				E.parent = iA;

				B.aabb = aabbCD;

				B.height = 1 + std::max(C.height, D.height);
				A.height = 1 + std::max(B.height, E.height);
				B.categoryBits = C.categoryBits | D.categoryBits;
				A.categoryBits = B.categoryBits | E.categoryBits;
				B.flags |= (C.flags | D.flags) & b3_enlargedNode;
				A.flags |= (B.flags | E.flags) & b3_enlargedNode;
				break;

			default:
				assert(false);
				break;
		}
	}
}

// It would be nicer if the root had zero height but maintaining this would drastically increase
// insertion cost because whole sub-trees would need the height to be updated.
void b3DynamicTree::insertLeaf(NodeIndex leaf, bool shouldRotate)
{
	if(_root == B3_NULL_INDEX )
	{
		_root = leaf;
		_nodes[_root].parent = B3_NULL_INDEX;
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

	if(oldParent != B3_NULL_INDEX)
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
	while(index != B3_NULL_INDEX)
	{
		auto child1 = _nodes[index].children.child1;
		auto child2 = _nodes[index].children.child2;

		assert(child1 != B3_NULL_INDEX);
		assert(child2 != B3_NULL_INDEX);

		_nodes[index].aabb = math::envelop(_nodes[child1].aabb, _nodes[child2].aabb);
		_nodes[index].categoryBits = _nodes[child1].categoryBits | _nodes[child2].categoryBits;
		_nodes[index].height = 1 + std::max(_nodes[child1].height, _nodes[child2].height );
		_nodes[index].flags |= ( _nodes[child1].flags | _nodes[child2].flags ) & b3_enlargedNode;

		if(shouldRotate)
			rotateNodes(index);

		index = _nodes[index].parent;
	}
}

void b3DynamicTree::removeLeaf(NodeIndex leaf)
{
	if(leaf == _root)
	{
		_root = B3_NULL_INDEX;
		return;
	}

	auto parent = _nodes[leaf].parent;
	auto grandParent = _nodes[parent].parent;
	NodeIndex sibling;
	if(_nodes[parent].children.child1 == leaf)
		sibling = _nodes[parent].children.child2;
	else
		sibling = _nodes[parent].children.child1;

	if(grandParent != B3_NULL_INDEX)
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
		while(index != B3_NULL_INDEX)
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
		_nodes[sibling].parent = B3_NULL_INDEX;
		freeNode(parent);
	}
}

// Create a proxy in the tree as a leaf node. We return the index of the node instead of a pointer so that we can grow
// the node pool.
b3DynamicTree::ProxyID b3DynamicTree::newProxy(bounds::AABB aabb, uint64_t categoryBits, uint64_t userData)
{
	assert(math::valid(aabb));

	auto proxyId = allocateNode();
	auto &node = _nodes[proxyId];

	node.aabb = aabb;
	node.userData = userData;
	node.categoryBits = categoryBits;
	node.height = 0;
	node.flags = b3_allocatedNode | b3_leafNode;

	bool shouldRotate = true;
	insertLeaf(proxyId, shouldRotate);

	++_proxyCount;

	return proxyId;
}

void b3DynamicTree::deleteProxy(ProxyID proxyId)
{
	assert(0 <= proxyId and proxyId < _nodeCapacity);
	assert(_nodes[proxyId].isLeaf());

	removeLeaf(proxyId);
	freeNode(proxyId);

	assert(_proxyCount > 0);
	--_proxyCount;
}

void b3DynamicTree::moveProxy(ProxyID proxyId, bounds::AABB aabb)
{
	assert(math::valid(aabb));
	assert(0 <= proxyId and proxyId < _nodeCapacity );
	assert(_nodes[proxyId].isLeaf());

	removeLeaf(proxyId);

	_nodes[proxyId].aabb = aabb;

	bool shouldRotate = false;
	insertLeaf(proxyId, shouldRotate);
}

void b3DynamicTree::growProxy(ProxyID proxyId, bounds::AABB aabb)
{
	assert(math::valid(aabb));
	assert(0 <= proxyId and proxyId < _nodeCapacity);
	assert(_nodes[proxyId].isLeaf());

	// Caller must ensure this

	assert(not intersect::check(_nodes[proxyId].aabb, aabb));

	auto &node = _nodes[proxyId];
	node.aabb = aabb;

	auto parentIndex = node.parent;
	while(parentIndex != B3_NULL_INDEX)
	{
		auto node = _nodes[parentIndex];
		bool changed = node.aabb.expand(aabb);

		// todo not sure why this node is marked as enlarged even if it didn't change
		node.flags |= b3_enlargedNode;

		parentIndex = node.parent;

		if(not changed)
			break;
	}

	while(parentIndex != B3_NULL_INDEX )
	{
		auto node = _nodes[parentIndex];
		if(node.flags & b3_enlargedNode )
			// early out because this ancestor was previously ascended and marked as enlarged
			break;

		node.flags |= b3_enlargedNode;
		parentIndex = node.parent;
	}
}

void b3DynamicTree::setCategoryBits(ProxyID proxyId, uint64_t categoryBits)
{
	assert(_nodes[proxyId].isLeaf());

	_nodes[proxyId].categoryBits = categoryBits;

	// Fix up category bits in ancestor internal _nodes
	auto nodeIndex = _nodes[proxyId].parent;
	while(nodeIndex != B3_NULL_INDEX )
	{
		auto &node = _nodes[nodeIndex];
		auto child1 = node.children.child1;
		assert(child1 != B3_NULL_INDEX);
		auto child2 = node.children.child2;
		assert(child2 != B3_NULL_INDEX);
		node.categoryBits = _nodes[child1].categoryBits | _nodes[child2].categoryBits;

		nodeIndex = node.parent;
	}
}

uint64_t b3DynamicTree::categoryBits(ProxyID proxyId) const
{
	assert( 0 <= proxyId and proxyId < _nodeCapacity);
	return _nodes[proxyId].categoryBits;
}

int b3DynamicTree::height() const
{
	if(_root == B3_NULL_INDEX)
		return 0;

	return _nodes[_root].height;
}

float b3DynamicTree::areaRatio() const
{
	if(_root == B3_NULL_INDEX)
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

bounds::AABB b3DynamicTree::rootBounds() const
{
	if(_root != B3_NULL_INDEX)
		return _nodes[_root].aabb;

	return {};
}

// Compute the height of a sub-tree.
int b3DynamicTree::computeHeightRecurse(NodeIndex nodeId) const
{
	assert( 0 <= nodeId and nodeId < _nodeCapacity);
	auto &node = _nodes[nodeId];

	if(node.isLeaf())
		return 0;

	auto height1 = computeHeightRecurse(node.children.child1);
	auto height2 = computeHeightRecurse(node.children.child2);
	return 1 + std::max(height1, height2);
}

int b3DynamicTree::computeHeight() const
{
	return computeHeightRecurse(_root);
}

void b3DynamicTree::validateStructure(NodeIndex index) const
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

void b3DynamicTree::validateMetrics(NodeIndex index) const
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

void b3DynamicTree::validate()
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

void b3DynamicTree::validateNoGrown() const
{
	for(auto i = 0u; i < _nodeCapacity; ++i)
	{
		const auto &node = _nodes[i];
		if(node.flags & b3_allocatedNode)
			assert((node.flags & b3_enlargedNode) == 0);
	}
}

size_t b3DynamicTree::byteSize() const
{
	auto size = sizeof(decltype(this)) + sizeof(decltype(_nodes)::value_type) * _nodes.capacity();
				  /* TODO: rebuild size_t(_rebuildCapacity) *  (sizeof(uint32_t) + sizeof(bounds::AABB) + sizeof(glm::vec3) + sizeof(uint32_t)); */

	return size;
}

b3TreeStats b3DynamicTree::query(bounds::AABB aabb, uint64_t maskBits, bool requireAllBits, QueryCb callback) const
{
	b3TreeStats result {
		.nodeVisits = 0,
		.leafVisits = 0,
	};

	if(not _nodeCount)
		return result;

	NodeIndex stack[B3_TREE_STACK_SIZE];
	uint32_t stackCount { 0 };
	stack[stackCount++] = _root;

	while(stackCount > 0)
	{
		auto nodeId = stack[--stackCount];
		if(nodeId == B3_NULL_INDEX)
		{
			// todo huh?
			assert(false);
			continue;
		}

		const auto &node = _nodes[nodeId];
		++result.nodeVisits;

		// Assuming branch prediction deals with requireAllBits well
		auto bitMatch = requireAllBits? (node.categoryBits & maskBits) == maskBits: (node.categoryBits & maskBits);

		if(bitMatch and intersect::check(node.aabb, aabb))
		{
			if(node.isLeaf())
			{
				// callback to user code with proxy id
				bool proceed = callback(nodeId, node.userData);
				++result.leafVisits;

				if(not proceed)
					return result;
			}
			else
			{
				assert(stackCount < B3_TREE_STACK_SIZE - 1);
				if(stackCount < B3_TREE_STACK_SIZE - 1)
				{
					stack[stackCount++] = node.children.child1;
					stack[stackCount++] = node.children.child2;
				}
			}
		}
	}

	return result;
}

float b3DynamicTree::nodeDistanceSq(glm::vec3 point, const b3TreeNode &node) const
{
	const auto r = point - glm::clamp(point, node.aabb.min(), node.aabb.max());
	return glm::dot(r, r);
}

struct b3QueryClosestItem
{
	b3DynamicTree::NodeIndex nodeIndex;
	float distanceToNodeSqr;
};

b3TreeStats b3DynamicTree::queryClosest(glm::vec3 point, uint64_t maskBits, bool requireAllBits, QueryClosestCb callback, float &minDistanceSqr) const
{
	b3TreeStats result {
		.nodeVisits = 0,
		.leafVisits = 0,
	};

	if(not _nodeCount)
		return result;

	float minSqr = minDistanceSqr;
	b3QueryClosestItem stack[B3_TREE_STACK_SIZE];
	auto stackCount = 0u;

	float rootDistanceSqr = nodeDistanceSq(point, _nodes[_root]);
	stack[stackCount++] = {
		.nodeIndex = _root,
		.distanceToNodeSqr = rootDistanceSqr,
	};

	while( stackCount > 0)
	{
		b3QueryClosestItem item = stack[--stackCount];
		const auto &node = _nodes[item.nodeIndex];
		++result.nodeVisits;

		auto bitMatch = requireAllBits? (node.categoryBits & maskBits) == maskBits: (node.categoryBits & maskBits);

		if(bitMatch)
		{
			if(item.distanceToNodeSqr < minSqr)
			{
				if(node.isLeaf())
				{
					// callback to user code with minimum distance squared so far and proxy id
					float dd = callback(minSqr, item.nodeIndex, node.userData);

					if(dd < minSqr)
						minSqr = dd;

					++result.leafVisits;
				}
				else
				{
					assert(stackCount < B3_TREE_STACK_SIZE - 1);
					if( stackCount < B3_TREE_STACK_SIZE - 1)
					{
						auto child1 = node.children.child1;
						auto child2 = node.children.child2;

						// Store the distance to node in the stack instead of recomputing after pop
						b3QueryClosestItem item1 = {
							.nodeIndex = child1,
							.distanceToNodeSqr = nodeDistanceSq( point, _nodes[child1]),
						};

						b3QueryClosestItem item2 = {
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

	return result;
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


b3TreeStats b3DynamicTree::castRay(const b3RayCastInput &input, uint64_t maskBits, bool requireAllBits, CastRayCb callback) const
{
	b3TreeStats stats {
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

	NodeIndex stack[B3_TREE_STACK_SIZE];
	auto stackCount = 0u;
	stack[stackCount++] = _root;

	auto subInput = input;

	while(stackCount > 0)
	{
		auto nodeId = stack[--stackCount];
		if(nodeId == B3_NULL_INDEX)
		{
			// todo is this possible?
			assert(false);
			continue;
		}

		const auto &node = _nodes[nodeId];
		++stats.nodeVisits;

		auto nodeAABB = node.aabb;

		// todo look at disassembly
		uint64_t bitMatch = requireAllBits? (node.categoryBits & maskBits ) == maskBits: (node.categoryBits & maskBits);

		if(bitMatch == 0 or not intersect::check(nodeAABB, segmentAABB))
			continue;

		auto lower = nodeAABB.min();
		auto upper = nodeAABB.max();

		bool edgeOverlap = testBoundsRayOverlap(lower, upper, p1, d);
		if(not edgeOverlap)
			continue;

		if(node.isLeaf())
		{
			subInput.maxFraction = maxFraction;

			float value = callback(subInput, nodeId, node.userData);
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
			assert(stackCount < B3_TREE_STACK_SIZE - 1);
			if(stackCount < B3_TREE_STACK_SIZE - 1)
			{
				auto c1 = _nodes[node.children.child1].aabb.center();
				auto c2 = _nodes[node.children.child2].aabb.center();
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


b3TreeStats b3DynamicTree::castBox(const b3BoxCastInput &input, uint64_t maskBits, bool requireAllBits, CastBoxCb callback) const
{
	b3TreeStats stats {
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

	// b3V32 pv1 = b3LoadV( &p1.x );
	// b3V32 dv = b3LoadV( &d.x );
	// b3V32 ev = b3LoadV( &extension.x );

	float maxFraction = input.maxFraction;

	// Build total box for the cast
	glm::vec3 t = input.translation * maxFraction;
	bounds::AABB totalAABB = {
		glm::min(originAABB.min(), originAABB.min() + t),
		glm::max(originAABB.max(), originAABB.max() + t),
	};

	auto subInput = input;

	NodeIndex stack[B3_TREE_STACK_SIZE];
	auto stackCount = 0u;
	stack[stackCount++] = _root;

	while(stackCount > 0)
	{
		auto nodeId = stack[--stackCount];
		if(nodeId == B3_NULL_INDEX)
		{
			assert(false);
			continue;
		}

		const auto &node = _nodes[nodeId];
		++stats.nodeVisits;

		uint64_t bitMatch = requireAllBits? (node.categoryBits & maskBits) == maskBits: (node.categoryBits & maskBits);

		if(not bitMatch or not intersect::check(node.aabb, totalAABB))
			continue;

		// radius extension is added to the node in this case
		glm::vec3 lower = node.aabb.min() - extension;
		glm::vec3 upper = node.aabb.max() + extension;
		bool edgeOverlap = testBoundsRayOverlap(lower, upper, p1, d);
		if(not edgeOverlap)
			continue;

		if(node.isLeaf())
		{
			subInput.maxFraction = maxFraction;

			float value = callback(subInput, nodeId, node.userData);
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
			assert(stackCount < B3_TREE_STACK_SIZE - 1);
			if(stackCount < B3_TREE_STACK_SIZE - 1)
			{
				glm::vec3 c1 = _nodes[node.children.child1].aabb.center();
				glm::vec3 c2 = _nodes[node.children.child2].aabb.center();
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

// Median split == 0, Surface area heuristic == 1
// #define B3_TREE_HEURISTIC 0

// #if B3_TREE_HEURISTIC == 0

// Median split heuristic
/* TODO: rebuild
uint32_t b3DynamicTree::partitionMid(NodeIndex *indices, glm::vec3 *centers, uint32_t count)
{
	// Handle trivial case
	if(count <= 2)
		return count / 2;

	glm::vec3 lowerBound = centers[0];
	glm::vec3 upperBound = centers[0];

	for(auto idx = 1u; idx < count; ++idx)
	{
		lowerBound = glm::min(lowerBound, centers[idx]);
		upperBound = glm::max(upperBound, centers[idx]);
	}

	glm::vec3 d = upperBound - lowerBound;
	glm::vec3 c = (lowerBound + upperBound) * 0.5f;

	// Partition longest axis using the Hoare partition scheme
	// https://en.wikipedia.org/wiki/Quicksort
	// https://nicholasvadivelu.com/2021/01/11/array-partition/
	auto i1 = 0u;
	auto i2 = count;
	if(d.x >= d.y and d.x >= d.z)
	{
		float pivot = c.x;

		while(i1 < i2)
		{
			while(i1 < i2 and centers[i1].x < pivot )
				++i1;

			while(i1 < i2 and centers[i2 - 1].x >= pivot)
				--i2;

			if(i1 < i2)
			{
				std::swap(indices[i1], indices[i2 - 1]);
				std::swap(centers[i1], centers[i2 - 1]);

				++i1;
				--i2;
			}
		}
	}
	else if(d.y >= d.z)
	{
		float pivot = c.y;

		while(i1 < i2)
		{
			while(i1 < i2 and centers[i1].y < pivot)
				++i1;

			while(i1 < i2 and centers[i2 - 1].y >= pivot)
				--i2;

			if(i1 < i2)
			{
				std::swap(indices[i1], indices[i2 - 1]);
				std::swap(centers[i1], centers[i2 - 1]);

				++i1;
				--i2;
			}
		}
	}
	else
	{
		float pivot = c.z;

		while(i1 < i2)
		{
			while(i1 < i2 and centers[i1].z < pivot)
				++i1;

			while(i1 < i2 and centers[i2 - 1].z >= pivot)
				--i2;

			if(i1 < i2)
			{
				std::swap(indices[i1], indices[i2 - 1]);
				std::swap(centers[i1], centers[i2 - 1]);

				++i1;
				--i2;
			}
		}
	}
	assert(i1 == i2);

	if(i1 > 0 and i1 < count)
		return i1;

	return count / 2;
}
*/
#define B3_BIN_COUNT 8

struct b3TreeBin
{
	bounds::AABB aabb;
	uint32_t count;
};

struct b3TreePlane
{
	bounds::AABB leftAABB;
	bounds::AABB rightAABB;
	uint32_t leftCount;
	uint32_t rightCount;
};

/* TODO: rebuild
// "On Fast Construction of SAH-based Bounding Volume Hierarchies" by Ingo Wald
// Returns the left child count
uint32_t b3DynamicTree::b3PartitionSAH(NodeIndex *indices, NodeIndex *binIndices, bounds::AABB *boxes, uint32_t count)
{
	assert(count > 0);

	b3TreeBin bins[B3_BIN_COUNT];
	b3TreePlane planes[B3_BIN_COUNT - 1];

	auto center = boxes[0].center();
	bounds::AABB centroidAABB { center, center };

	for(auto idx = 1u; idx < count; ++idx)
	{
		center = boxes[idx].center();
		centroidAABB.min() = glm::min(centroidAABB.min(), center);
		centroidAABB.max() = glm::max(centroidAABB.max(), center);
	}

	glm::vec3 d = centroidAABB.max() - centroidAABB.min();

	// Find longest axis
	uint32_t axisIndex;
	float invD;
	if(d.x > d.y)
	{
		axisIndex = 0;
		invD = d.x;
	}
	else
	{
		axisIndex = 1;
		invD = d.y;
	}

	invD = invD > 0.0f ? 1.0f / invD : 0.0f;

	// Initialize bin bounds and count
	for(auto idx = 0u; idx < B3_BIN_COUNT; ++idx)
	{
		bins[idx].aabb.min() = glm::vec3(std::numeric_limits<float>::max());
		bins[idx].aabb.max() = glm::vec3(std::numeric_limits<float>::lowest());
		bins[idx].count = 0;
	}

	// Assign boxes to bins and compute bin boxes
	// TODO_ERIN optimize
	float binCount = B3_BIN_COUNT;
	float lowerBoundArray[2] = { centroidAABB.lowerBound.x, centroidAABB.lowerBound.y };
	float minC = lowerBoundArray[axisIndex];
	for ( int i = 0; i < count; ++i )
	{
		b3Vec3 c = b3AABB_Center( boxes[i] );
		float cArray[2] = { c.x, c.y };
		int binIndex = (int)( binCount * ( cArray[axisIndex] - minC ) * invD );
		binIndex = b3ClampInt( binIndex, 0, B3_BIN_COUNT - 1 );
		binIndices[i] = binIndex;
		bins[binIndex].count += 1;
		bins[binIndex].aabb = b3AABB_Union( bins[binIndex].aabb, boxes[i] );
	}

	int planeCount = B3_BIN_COUNT - 1;

	// Prepare all the left planes, candidates for left child
	planes[0].leftCount = bins[0].count;
	planes[0].leftAABB = bins[0].aabb;
	for ( int i = 1; i < planeCount; ++i )
	{
		planes[i].leftCount = planes[i - 1].leftCount + bins[i].count;
		planes[i].leftAABB = b3AABB_Union( planes[i - 1].leftAABB, bins[i].aabb );
	}

	// Prepare all the right planes, candidates for right child
	planes[planeCount - 1].rightCount = bins[planeCount].count;
	planes[planeCount - 1].rightAABB = bins[planeCount].aabb;
	for ( int i = planeCount - 2; i >= 0; --i )
	{
		planes[i].rightCount = planes[i + 1].rightCount + bins[i + 1].count;
		planes[i].rightAABB = b3AABB_Union( planes[i + 1].rightAABB, bins[i + 1].aabb );
	}

	// Find best split to minimize SAH
	float minCost = FLT_MAX;
	int bestPlane = 0;
	for ( int i = 0; i < planeCount; ++i )
	{
		float leftArea = b3Perimeter( planes[i].leftAABB );
		float rightArea = b3Perimeter( planes[i].rightAABB );
		int leftCount = planes[i].leftCount;
		int rightCount = planes[i].rightCount;

		float cost = leftCount * leftArea + rightCount * rightArea;
		if ( cost < minCost )
		{
			bestPlane = i;
			minCost = cost;
		}
	}

	// Partition node indices and boxes using the Hoare partition scheme
	// https://en.wikipedia.org/wiki/Quicksort
	// https://nicholasvadivelu.com/2021/01/11/array-partition/
	int i1 = 0, i2 = count;
	while ( i1 < i2 )
	{
		while ( i1 < i2 && binIndices[i1] < bestPlane )
		{
			i1 += 1;
		};

		while ( i1 < i2 && binIndices[i2 - 1] >= bestPlane )
		{
			i2 -= 1;
		};

		if ( i1 < i2 )
		{
			// Swap indices
			{
				int temp = indices[i1];
				indices[i1] = indices[i2 - 1];
				indices[i2 - 1] = temp;
			}

			// Swap boxes
			{
				b3AABB temp = boxes[i1];
				boxes[i1] = boxes[i2 - 1];
				boxes[i2 - 1] = temp;
			}

			i1 += 1;
			i2 -= 1;
		}
	}
	B3_ASSERT( i1 == i2 );

	if ( i1 > 0 && i1 < count )
	{
		return i1;
	}
	else
	{
		return count / 2;
	}
}

*/

/* TODO: rebuild
// Temporary data used to track the rebuild of a tree node
struct b3RebuildItem
{
	b3DynamicTree::NodeIndex nodeIndex;
	uint32_t childCount;

	// Leaf indices
	b3DynamicTree::NodeIndex startIndex;
	b3DynamicTree::NodeIndex splitIndex;
	b3DynamicTree::NodeIndex endIndex;
};

// Returns root node index
b3DynamicTree::NodeIndex b3DynamicTree::buildTree(uint32_t leafCount)
{
	if(leafCount == 1)
	{
		nodes[leafIndices[0]].parent = B3_NULL_INDEX;
		return leafIndices[0];
	}

	// todo large stack item
	struct b3RebuildItem stack[B3_TREE_STACK_SIZE];
	int top = 0;

	stack[0].nodeIndex = allocateNode();
	stack[0].childCount = std::numeric_limits<uint32_t>::max();
	stack[0].startIndex = 0;
	stack[0].endIndex = leafCount;
#if B3_TREE_HEURISTIC == 0
	stack[0].splitIndex = partitionMid(leafIndices.data(), leafCenters.data(), leafCount);
#else
	stack[0].splitIndex = partitionSAH(leafIndices, binIndices, leafBoxes, leafCount);
#endif

	while(true)
	{
		auto &item = stack[top];

		item.childCount += 1;

		if(item.childCount == 2)
		{
			// This internal node has both children established

			if(top == 0)  // all done
				break;

			auto &parentItem = stack[top - 1];
			auto &parentNode = nodes[parentItem.nodeIndex];

			if(parentItem.childCount == 0)
			{
				assert( parentNode.children.child1 == B3_NULL_INDEX);
				parentNode.children.child1 = item.nodeIndex;
			}
			else
			{
				assert(parentItem.childCount == 1);
				assert(parentNode.children.child2 == B3_NULL_INDEX);
				parentNode.children.child2 = item.nodeIndex;
			}

			auto &node = nodes[item.nodeIndex];

			assert(node.parent == B3_NULL_INDEX);
			node.parent = parentItem.nodeIndex;

			assert(node.children.child1 != B3_NULL_INDEX);
			assert(node.children.child2 != B3_NULL_INDEX);
			auto &child1 = nodes[node.children.child1];
			auto &child2 = nodes[node.children.child2];

			node.aabb = math::envelop(child1.aabb, child2.aabb);
			node.height = 1 + std::max(child1.height, child2.height );
			node.categoryBits = child1.categoryBits | child2.categoryBits;

			// Pop stack
			++top;
		}
		else
		{
			NodeIndex startIndex, endIndex;
			if(item.childCount == 0)
			{
				startIndex = item.startIndex;
				endIndex = item.splitIndex;
			}
			else
			{
				assert(item.childCount == 1);
				startIndex = item.splitIndex;
				endIndex = item.endIndex;
			}

			auto count = endIndex - startIndex;

			if(count == 1)
			{
				auto childIndex = leafIndices[startIndex];
				auto &node = nodes[item.nodeIndex];

				if(item.childCount == 0)
				{
					assert(node.children.child1 == B3_NULL_INDEX);
					node.children.child1 = childIndex;
				}
				else
				{
					assert(item.childCount == 1);
					assert(node.children.child2 == B3_NULL_INDEX);
					node.children.child2 = childIndex;
				}

				auto &childNode = nodes[childIndex];
				assert(childNode.parent == B3_NULL_INDEX);
				childNode.parent = item.nodeIndex;
			}
			else
			{
				assert(count > 0);
				assert(top < B3_TREE_STACK_SIZE);

				++top;
				auto &newItem = stack[top];
				newItem.nodeIndex = allocateNode();
				newItem.childCount = std::numeric_limits<uint32_t>::max();;
				newItem.startIndex = startIndex;
				newItem.endIndex = endIndex;
#if B3_TREE_HEURISTIC == 0
				newItem.splitIndex = partitionMid(&leafIndices[startIndex], &leafCenters[startIndex], count);
#else
				newItem.splitIndex = partitionSAH(leafIndices[startIndex], binIndices[startIndex], leafBoxes[startIndex], count);
#endif
				newItem.splitIndex += startIndex;
			}
		}
	}

	auto &rootNode = nodes[stack[0].nodeIndex];
	assert(rootNode.parent == B3_NULL_INDEX);
	assert(rootNode.children.child1 != B3_NULL_INDEX);
	assert(rootNode.children.child2 != B3_NULL_INDEX);

	auto &child1 = nodes[rootNode.children.child1];
	auto &child2 = nodes[rootNode.children.child2];

	rootNode.aabb = math::envelop(child1.aabb, child2.aabb);
	rootNode.height = 1 + std::max(child1.height, child2.height );
	rootNode.categoryBits = child1.categoryBits | child2.categoryBits;

	return stack[0].nodeIndex;
}
*/

// Not safe to access tree during this operation because it may grow
/* TODO: rebuild
uint32_t b3DynamicTree::rebuild(bool fullBuild)
{
	if(not proxyCount)
		return 0;

	// Ensure capacity for rebuild space
	if(proxyCount > rebuildCapacity)
	{
		auto newCapacity = proxyCount + proxyCount/2;

		// b3Free( tree->leafIndices, tree->rebuildCapacity * sizeof( int ) );
		// tree->leafIndices = (int*)b3Alloc( newCapacity * sizeof( int ) );
		leafIndices.resize(newCapacity);

#if B3_TREE_HEURISTIC == 0
		// b3Free( tree->leafCenters, tree->rebuildCapacity * sizeof( b3Vec3 ) );
		// tree->leafCenters = (b3Vec3*)b3Alloc( newCapacity * sizeof( b3Vec3 ) );
		leafCenters.resize(newCapacity);
#else
		// b3Free( tree->leafBoxes, tree->rebuildCapacity * sizeof( b3AABB ) );
		// tree->leafBoxes = (b3AABB*)b3Alloc( newCapacity * sizeof( b3AABB ) );
		leafBoxes.resize(newCapacity);
		// b3Free( tree->binIndices, tree->rebuildCapacity * sizeof( int ) );
		// tree->binIndices = (int*)b3Alloc( newCapacity * sizeof( int ) );
		binIndices.resize(newCapacity);
#endif
		rebuildCapacity = newCapacity;
	}

	auto leafCount = 0u;
	NodeIndex stack[B3_TREE_STACK_SIZE];
	auto stackCount = 0u;

	auto nodeIndex = root;
	auto *node = &nodes[nodeIndex];

	// These are the nodes that get sorted to rebuild the tree.
	// I'm using indices because the node pool may grow during the build.

#if B3_TREE_HEURISTIC == 0
	// b3Vec3* leafCenters = tree->leafCenters;
#else
	// b3AABB* leafBoxes = tree->leafBoxes;
#endif

	// Gather all proxy nodes that have grown and all internal nodes that haven't grown. Both are
	// considered leaves in the tree rebuild.
	// Free all internal nodes that have grown.
	// todo use a node growth metric instead of simply enlarged to reduce rebuild size and frequency
	// this should be weighed against B3_MAX_AABB_MARGIN
	while(true)
	{
		if(node->isLeaf() or (not fullBuild and (node->flags & b3_enlargedNode) == 0))
		{
			leafIndices[leafCount] = nodeIndex;
#if B3_TREE_HEURISTIC == 0
			leafCenters[leafCount] = node->aabb.center();
#else
			leafBoxes[leafCount] = node.aabb;
#endif
			++leafCount;

			// Detach
			node->parent = B3_NULL_INDEX;
		}
		else
		{
			auto doomedNodeIndex = nodeIndex;

			// Handle children
			nodeIndex = node->children.child1;

			assert(stackCount < B3_TREE_STACK_SIZE);
			if(stackCount < B3_TREE_STACK_SIZE)
				stack[stackCount++] = node->children.child2;

			node = &nodes[nodeIndex];

			// Remove doomed node
			freeNode(doomedNodeIndex);

			continue;
		}

		if(stackCount == 0)
			break;

		nodeIndex = stack[--stackCount];
		node = &nodes[nodeIndex];
	}

#if B3_ENABLE_VALIDATION != 1
	auto capacity = nodeCapacity;
	for(auto idx = 0u; idx < capacity; ++idx)
	{
		if(nodes[idx].flags & b3_allocatedNode )
			assert((nodes[idx].flags & b3_enlargedNode ) == 0);
	}
#endif

	assert(leafCount <= proxyCount);

	>root = buildTree(leafCount);

	validate();

	return leafCount;
}
*/

} // RGL