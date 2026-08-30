// SPDX-FileCopyrightText: 2025 Erin Catto
// SPDX-License-Identifier: MIT

namespace RGL
{


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




// TODO: rebuild



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