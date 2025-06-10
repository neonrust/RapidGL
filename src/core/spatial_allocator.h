#pragma once

#include "container_types.h"
#include <cstdint>
#include <glm/integer.hpp>
#include <vector>
#include <print>

/*
0-based linear indexing:

 L 0       L 1         L 2  (child of node 2)
 +-----+   +-----+     +-----+
 |     |   | 1| 2|     | 9|10|
 |  0  |   +--+--+ ... +--+--+   and so on...
 |     |   | 3| 4|     |11|12|
 +-----+   +--+--+     +--+--+

	  parent:  (index - 1) >> 2
   top right:  index << 2 + 1
	top left:  index << 2 + 2
 bottom left:  index << 2 + 3
bottom right:  index << 2 + 4

 # levels: log2( <max size> / <min size> )
*/

// see https://gcc.gnu.org/onlinedocs/gcc/Bit-Operation-Builtins.html

namespace RGL
{

template<typename AxisT=uint32_t>
class SpatialAllocator
{
public:
	using SizeT = AxisT;
	using NodeIndex = uint32_t;
	static constexpr auto BadIndex = NodeIndex(-1);

	enum class NodeChild : uint32_t
	{
		TopLeft      = 1,
		TopRight     = 2,
		BottomLeft   = 3,
		BottomRight  = 4
	};
	struct Node
	{
		uint32_t children_allocated;
		bool     allocated;
	};

	struct Rect
	{
		AxisT x;
		AxisT y;
		AxisT w;
		AxisT h;
	};

	using AllocatedSlots = dense_map<AxisT, size_t>;

public:
	SpatialAllocator(AxisT size, AxisT min_block_size=AxisT{1}, AxisT max_block_size=AxisT(0));

	[[nodiscard]] inline AxisT size() const { return _size; }

	[[nodiscard]] NodeIndex allocate(AxisT size);
	[[nodiscard]] NodeIndex allocate(AxisT size, AxisT min_size);  // demote to smaller size if necessary
	void free(NodeIndex index);

	[[nodiscard]] Rect rect(NodeIndex index);

	[[nodiscard]] inline const AllocatedSlots &allocated() const { return _allocated; }

	[[nodiscard]] uint32_t level(NodeIndex index) const;
	[[nodiscard]] AxisT size(NodeIndex index) const;

	[[nodiscard]] inline const Node &node(NodeIndex index) const { return _nodes[index]; }
	[[nodiscard]] inline const Node &operator [] (NodeIndex index) const { return node(index); }

	[[nodiscard]] static inline NodeIndex parent_index(NodeIndex index) { return index > 0? (index - 1) >> 2: BadIndex; }
	[[nodiscard]] static inline NodeIndex child_index(NodeIndex index, NodeChild child=NodeChild::TopRLeft) {
		return NodeIndex((uint32_t(index) << 2) + uint32_t(child));
	}
	[[nodiscard]] static inline NodeChild node_child(NodeIndex index) { return NodeChild(uint32_t(index) & 3); }

	[[nodiscard]] inline NodeIndex end() const { return BadIndex; }

private:
	[[nodiscard]] inline uint32_t size_level(AxisT size) const { assert(__builtin_popcount(size) == 1 and size < _size); return uint32_t(__builtin_ffs(int(_size / size)) - 1); }
	[[nodiscard]] inline AxisT level_size(uint32_t level) const { return _size >> level; }
	[[nodiscard]] static inline uint32_t levels_nodes(uint32_t levels) { return 1 << 2*levels; };
	[[nodiscard]] static inline uint32_t level_start_index(uint32_t level) { return (levels_nodes(level) - 1)/3; };
	[[nodiscard]] static uint32_t index_level(NodeIndex index) {
		const auto leading_zeroes = static_cast<uint32_t>(__builtin_clz(index * 3 + 1));
		return (sizeof(index)*8 - leading_zeroes - 1)/2;
	}
	[[nodiscard]] static inline NodeIndex descendant_index(NodeIndex index, uint32_t skip_levels) {
		assert(skip_levels > 0);
		const auto current_level = index_level(index);
		const auto level_offset = index - level_start_index(current_level);
		const auto target_level_start = level_start_index(current_level + skip_levels);
		const auto descendants_per_node = 1u << (2 * skip_levels);
		const auto result = target_level_start + level_offset * descendants_per_node;
		return result;
	}
	[[nodiscard]] inline Node &_node(NodeIndex index) { return _nodes[index]; }
	[[nodiscard]] NodeIndex find_available(uint32_t target_level, uint32_t current_level=0, NodeIndex index=0);

private:
	std::vector<Node> _nodes;
	AxisT _size;
	AxisT _max_size;
	AxisT _min_size;
	AllocatedSlots _allocated;
};

template<typename AxisT>
inline SpatialAllocator<AxisT>::SpatialAllocator(AxisT size, AxisT min_block_size, AxisT max_block_size) :
	_size(size),
	_max_size(max_block_size > 0? max_block_size: size),
	_min_size(min_block_size)
{
	// sizes must be power of 2
	assert(__builtin_popcount(_size) == 1);
	assert(__builtin_popcount(_min_size) == 1);
	assert(__builtin_popcount(_max_size) == 1);
	assert(_min_size < _size);

	auto num_levels = size_level(_min_size);
	auto num_nodes = level_start_index(num_levels);
	assert(num_nodes < 65536); // more nodes seems a bit excessive don't you think?
	_nodes.resize(num_nodes);

	_allocated.reserve(num_levels);

	std::print("spatial allocator: {}-{}  ->  {} nodes, {} levels\n", _min_size, _size, num_nodes, num_levels);
}

template<typename AxisT>
inline SpatialAllocator<AxisT>::NodeIndex SpatialAllocator<AxisT>::allocate(AxisT size)
{
	return allocate(size, size);
}

template<typename AxisT>
inline SpatialAllocator<AxisT>::NodeIndex SpatialAllocator<AxisT>::allocate(AxisT size, AxisT min_size)
{
	assert(__builtin_popcount(size) == 1);
	assert(__builtin_popcount(min_size) == 1);

	min_size = std::min(min_size, size);

	if(size < _min_size or size > _max_size)
		return BadIndex;

	if(size < _min_size or size > _max_size)
		return BadIndex;
	min_size = std::max(min_size, _min_size);
	min_size = std::min(min_size, size);

	auto allocated_size = size;
	auto found = BadIndex;
	for(auto lvl = size_level(size); lvl <= size_level(min_size); ++lvl)
	{
		found = find_available(lvl);
		if(found != BadIndex)
			break;
		allocated_size >>= 1;
	}

	if(found != BadIndex)
	{
		auto &n = _node(found);
		n.allocated = true;
		// std::print("  [sa] {} demoted {} -> {}\n", found, size, allocated_size);
		++_allocated[allocated_size];

		// increment children_allocated for ancestors (when 'parent' wraps it becomes larger than 'found')
		for(auto parent = parent_index(found); parent >= 0 and parent < found; parent = parent_index(parent))
		{
			auto &p = _node(parent);
			++p.children_allocated;
			// std::print("  [sa] {}.children_allocated++ -> {}\n", parent, p.children_allocated);
		}

	}

	return found;
}

// static auto s_indent = 0;

template<typename AxisT>
SpatialAllocator<AxisT>::NodeIndex SpatialAllocator<AxisT>::find_available(uint32_t target_level, uint32_t current_level, NodeIndex index)
{
	// if(index == 0)
	// {
	// 	s_indent = 0;
	// 	std::print("  [sa] find @ level {}\n", target_level);
	// }

	const auto &n = _node(index);

	// skip branches that are fully allocated
	if (n.allocated or n.children_allocated == levels_nodes(target_level - current_level))
	{
		// std::print("{:{}}  [sa] {}  branch not available ({} + {})\n", "", s_indent, index, n.allocated, n.children_allocated);
		return BadIndex;
	}
	else if (current_level == target_level)
	{
		// if(n.children_allocated)
		// 	std::print("{:{}}  [sa] {}  not available ({} + {})\n", "", s_indent, index, n.allocated, n.children_allocated);
		// else
		// 	std::print("{:{}}  [sa] {} found!\n", "", s_indent, index);
		return n.children_allocated > 0? BadIndex: index;
	}

	// if the whole subtree is available, we can skip to the first child at target_level
	if(not n.allocated and n.children_allocated == 0)
	{
		const auto skip_to_index = descendant_index(index, target_level - current_level);
		assert(index_level(skip_to_index) == target_level);
		// std::print("{:{}}  [sa] direct hop {} -> {}  (level {} -> {})\n", "", s_indent, index, skip_to_index, current_level, target_level);
		return skip_to_index;//find_available(target_level, target_level, skip_to_index);
	}

	for(auto child = 1u; child <= 4; ++child)
	{
		// std::print("{:{}}  [sa] {} -> child {}\n", "", s_indent, index, child);
		// ++s_indent;
		auto found = find_available(target_level, current_level + 1, child_index(index, NodeChild(child)));
		// --s_indent;
		if (found != BadIndex)
			return found;
	}

	// std::print("{:{}}  [sa] {}  branch not available\n", "", s_indent, index);
	return BadIndex;
}

template<typename AxisT>
void SpatialAllocator<AxisT>::free(NodeIndex index)
{
	auto &n = _node(index);
	assert(n.allocated);
	assert(n.children_allocated == 0);
	n.allocated = false;

	const auto allocated_size = level_size(index_level(index));
	--_allocated[allocated_size];

	// decrement child allocation counter in ancestors
	while(index > 0)
	{
		index = parent_index(index);
		auto &n = _node(index);
		assert(n.children_allocated > 0);
		--n.children_allocated;
	}
}

template<typename AxisT>
inline SpatialAllocator<AxisT>::Rect SpatialAllocator<AxisT>::rect(NodeIndex index)
{
	const auto level = index_level(index);
	if(level == 0)
		return { 0, 0, _size, _size };

	const uint32_t lvl_relative_index = index - level_start_index(level);
	const uint32_t tiles_per_axis = 1 << level;
	const AxisT    tile_size = size(index);

	const AxisT tile_x = lvl_relative_index % tiles_per_axis;
	const AxisT tile_y = lvl_relative_index / tiles_per_axis;

	return Rect{
		.x = tile_x * tile_size,
		.y = tile_y * tile_size,
		.w = tile_size,
		.h = tile_size,
	};
}

template<typename AxisT>
inline uint32_t SpatialAllocator<AxisT>::level(NodeIndex index) const
{
	return static_cast<uint32_t>(std::floor(static_cast<uint32_t>(std::log2(1 + 3 * index)) >> 1));
}

template<typename AxisT>
inline AxisT SpatialAllocator<AxisT>::size(NodeIndex index) const
{
	return _size >> level(index);
}



} // RGL
