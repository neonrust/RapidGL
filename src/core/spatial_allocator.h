#pragma once

#include "container_types.h"
#include <cstdint>
#include <bit>
#include <glm/integer.hpp>
#include <glm/vec3.hpp>
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

namespace _private
{

// round the specified number up to the next power of 2 (i.e. if not already a power of 2)
template<typename T>
T round_up_pow2(T n)
{
	if(n == 0)
		return 1;
	if(__builtin_popcount(n) == 1) // already pow2
		return n;
	return T(1) << (sizeof(T)*8 -  __builtin_clz(n));
}

} // _private

template<typename AxisT=uint32_t>
class SpatialAllocator
{
public:
	using SizeT = AxisT;
	using NodeIndex = uint32_t;
	static constexpr auto BadIndex = NodeIndex(-1);

	// TODO: accessing this enums from the outside is cembersome...
	enum class NodeChild : uint32_t
	{
		TopLeft      = 1,  // NW
		TopRight     = 2,  // NE
		BottomLeft   = 3,  // SW
		BottomRight  = 4,  // SE
		Invalid      = 0,
	};
	struct Node
	{
		bool     allocated;
		uint32_t children_allocated;
	};

	struct Rect
	{
		SizeT x;
		SizeT y;
		SizeT w;
		SizeT h;
	};

	using AllocatedSlots = dense_map<AxisT, size_t>; // size -> count

	static constexpr AxisT default_max_size_shift = 3; // i.e. size/8    (1024 of 8192)
	static constexpr AxisT default_min_size_shift = 6; // i.e. size/64  (128 or 8192)

public:
	SpatialAllocator(AxisT size, AxisT min_block_size=AxisT(0), AxisT max_block_size=AxisT(0));

	void reset();  // free *all* allocated nodes

	inline SizeT max_size() const { return _max_size; };
	inline SizeT min_size() const { return _min_size; };
	inline uint32_t max_size_level() const { return level_from_size(_min_size); }
	inline uint32_t min_size_level() const { return level_from_size(_max_size); }

	size_t num_allocatable_levels() const;

	[[nodiscard]] inline AxisT size() const { return _size; }

	[[nodiscard]] NodeIndex allocate(AxisT size);
	[[nodiscard]] NodeIndex allocate(AxisT size, AxisT min_size);  // allow demotion to smaller size, if necessary
	bool free(NodeIndex index);

	[[nodiscard]] inline const AllocatedSlots &num_allocated() const { return _allocated; }
	[[nodiscard]] size_t num_allocated(AxisT size) const;

	[[nodiscard]] Rect rect(NodeIndex index);
	[[nodiscard]] inline AxisT size(NodeIndex index) const { return _size >> level(index); }

	// used as "bad index" in returns
	[[nodiscard]] inline NodeIndex end() const { return BadIndex; }
	[[nodiscard]] inline uint32_t level_from_size(AxisT size) const { assert(__builtin_popcount(size) == 1 and size < _size); return std::countr_zero(uint32_t(_size / size)); }

private:
	[[nodiscard]] uint32_t level(NodeIndex index) const;
	[[nodiscard]] NodeIndex parent_index(NodeIndex child);
	[[nodiscard]] NodeIndex child_index(NodeIndex parent, NodeChild child=NodeChild::TopRLeft);
	[[nodiscard]] NodeChild node_child(NodeIndex index);
	[[nodiscard]] inline AxisT size_at_level(uint32_t level) const { return _size >> level; }
	[[nodiscard]] static inline uint32_t num_nodes_in_level(uint32_t level) { return 1u << 2u*level; };
	[[nodiscard]] static inline uint32_t level_start_index(uint32_t level) { return (num_nodes_in_level(level) - 1)/3; };
	[[nodiscard]] static uint32_t level_from_index(NodeIndex index);
	// [[nodiscard]] static NodeIndex index_of_descendant(NodeIndex index, uint32_t skip_levels);

	// search the tree for a vacant "target_level"-sized slot
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
	_size(_private::round_up_pow2(size)),
	_max_size(max_block_size > 0? _private::round_up_pow2(max_block_size): _size >> default_max_size_shift),
	_min_size(min_block_size > 0? _private::round_up_pow2(min_block_size): _size >> default_min_size_shift)
{
	assert(__builtin_popcount(_size) == 1);
	assert(__builtin_popcount(_min_size) == 1);
	assert(__builtin_popcount(_max_size) == 1);
	assert(_min_size < _size);

	auto num_levels = level_from_size(_min_size);
	auto num_nodes = level_start_index(num_levels + 1); // == the number of nodes before that level
	assert(num_nodes < 65536); // more nodes seems a bit excessive don't you think?
	_nodes.resize(num_nodes);

	_allocated.reserve(num_levels);

	std::print("SpatialAllocator[{}..{}]: {} levels; {} nodes\n", _min_size, _max_size, num_allocatable_levels(), num_nodes);
}

template<typename AxisT>
void SpatialAllocator<AxisT>::reset()
{
	std::memset(_nodes.data(), 0, _nodes.size() * sizeof(Node));
	_allocated.clear();
}

template<typename AxisT>
size_t SpatialAllocator<AxisT>::num_allocatable_levels() const
{
	return 32 - __builtin_clz(max_size() / min_size());
}

template<typename AxisT>
SpatialAllocator<AxisT>::NodeIndex SpatialAllocator<AxisT>::allocate(AxisT size)
{
	return allocate(size, size);
}

template<typename AxisT>
SpatialAllocator<AxisT>::NodeIndex SpatialAllocator<AxisT>::allocate(AxisT size, AxisT min_size)
{
	size = _private::round_up_pow2(size);
	min_size = _private::round_up_pow2(min_size);
	assert(min_size <= size);

	if(size < _min_size or size > _max_size)
		return BadIndex;

	min_size = glm::clamp(min_size, _min_size, size);

	// auto allocated_size = size;
	const auto max_level = level_from_size(min_size);
	auto allocated_index = BadIndex;
	auto lvl = level_from_size(size) - 1;

	// TODO: optimization (if max_level < lvl): find_available() can "peek" if a lower level is possible
	while(allocated_index == BadIndex and lvl < max_level)
		allocated_index = find_available(++lvl);

	if(allocated_index != BadIndex)
	{
		const auto allocated_size = size_at_level(lvl);
		++_allocated[allocated_size];

		auto &n = _nodes[allocated_index];
		n.allocated = true;
		assert(n.children_allocated == 0);
		// std::print("  [sa] {} demoted {} -> {}\n", found, size, allocated_size);

		auto index = allocated_index;
		// increment 'children_allocated' for ancestors
		while(index > 0)
		{
			index = parent_index(index);
			auto &p = _nodes[index];
			assert(not p.allocated);
			++p.children_allocated;
			// std::print("  [sa] {}.children_allocated++ -> {}\n", node, p.children_allocated);
		}
	}

	return allocated_index;
}

// static auto s_indent = 0;

template<typename AxisT>
uint32_t SpatialAllocator<AxisT>::level_from_index(NodeIndex index)
{
	const auto leading_zeroes = static_cast<uint32_t>(__builtin_clz(index * 3 + 1));
	return (sizeof(index)*8 - leading_zeroes - 1)/2;
}

template<typename AxisT>
SpatialAllocator<AxisT>::NodeIndex SpatialAllocator<AxisT>::find_available(uint32_t target_level, uint32_t current_level, NodeIndex index)
{
	// if(index == 0)
	// {
	// 	s_indent = 0;
	// 	std::print("  [sa] find @ level {}\n", target_level);
	// }

	const auto &n = _nodes[index];

	// skip branches that are fully allocated
	if (n.allocated)// or n.children_allocated == num_nodes_in_levels(target_level - current_level))
	{
		// std::print("{:{}}  [sa] {}  branch not available ({} + {})\n", "", s_indent, index, n.allocated, n.children_allocated);
		return end();
	}
	if (current_level == target_level)
	{
		// if(n.children_allocated)
		// 	std::print("{:{}}  [sa] {}  not available ({} + {})\n", "", s_indent, index, n.allocated, n.children_allocated);
		// else
		// 	std::print("{:{}}  [sa] {} found!\n", "", s_indent, index);
		if(n.children_allocated == 0)
			return index;
		return end();
	}

	// if the whole subtree is available, we can skip to the first child at target_level
	// if(not n.allocated and n.children_allocated == 0)
	// {
	// 	const auto skip_to_index = index_of_descendant(index, target_level - current_level);
	// 	assert(level_from_index(skip_to_index) == target_level);
	// 	// if(target_level > current_level + 1)
	// 	// 	std::print("[sa] size={} direct hop {} -> {}  (level {} -> {})\n", size_at_level(target_level), index, skip_to_index, current_level, target_level);
	// 	return find_available(target_level, target_level, skip_to_index);
	// }

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
bool SpatialAllocator<AxisT>::free(NodeIndex index)
{
	if(index >= _nodes.size())
		return false;
	auto &n = _nodes[index];
	if(not n.allocated)
		return false;
	assert(n.children_allocated == 0);

	n.allocated = false;

	const auto allocated_size = size_at_level(level_from_index(index));
	--_allocated[allocated_size]; // remove the entry if 0?

	// decrement child allocation counter in ancestors
	while(index > 0)
	{
		index = parent_index(index);
		auto &n = _nodes[index];
		assert(n.children_allocated > 0);
		--n.children_allocated;
	}

	return true;
}

template<typename AxisT>
size_t SpatialAllocator<AxisT>::num_allocated(AxisT size) const
{
	if(auto found = _allocated.find(size); found != _allocated.end())
		return found->second;
	return 0;
}

template<typename AxisT>
SpatialAllocator<AxisT>::Rect SpatialAllocator<AxisT>::rect(NodeIndex index)
{
	if(index == 0)
		return { 0, 0, _size, _size };

	const auto parent_rect = rect(parent_index(index));

	const auto half_w = parent_rect.w >> 1;
	const auto half_h = parent_rect.h >> 1;

	Rect rect { parent_rect.x, parent_rect.y, half_w, half_h };

	switch (node_child(index))
	{
	case NodeChild::BottomRight:
		rect.y += half_h;
		// fallthrough
	case NodeChild::TopRight:
		rect.x += half_w;  break;
	case NodeChild::BottomLeft:
		rect.y += half_h;  break;
	}

	return rect;
}

template<typename AxisT>
uint32_t SpatialAllocator<AxisT>::level(NodeIndex index) const
{
	return static_cast<uint32_t>(std::floor(static_cast<uint32_t>(std::log2(1 + 3 * index)) >> 1));
}

template<typename AxisT>
typename SpatialAllocator<AxisT>::NodeIndex SpatialAllocator<AxisT>::parent_index(NodeIndex child)
{
	if(child == 0 or child >= _nodes.size())
	{
		assert(false);
		return BadIndex;
	}
	return (child - 1) >> 2;
}

template<typename AxisT>
typename SpatialAllocator<AxisT>::NodeIndex SpatialAllocator<AxisT>::child_index(NodeIndex parent, NodeChild child)
{
	if(size_at_level(level_from_index(parent)) <= _min_size)
	{
		assert(false);
		return BadIndex;
	}
	return NodeIndex((uint32_t(parent) << 2) + uint32_t(child));
}

template<typename AxisT>
typename SpatialAllocator<AxisT>::NodeChild SpatialAllocator<AxisT>::node_child(NodeIndex index)
{
	if(index == 0 or index >= _nodes.size())
		return NodeChild::Invalid;
	return NodeChild((uint32_t(index - 1) & 3) + 1);
}
/*
template<typename AxisT>
SpatialAllocator<AxisT>::NodeIndex SpatialAllocator<AxisT>::index_of_descendant(NodeIndex index, uint32_t skip_levels) {
	assert(skip_levels > 0);
	const auto current_level = level_from_index(index);
	const auto level_offset = index - level_start_index(current_level);
	const auto target_level_start = level_start_index(current_level + skip_levels);
	const auto descendants_per_node = 1u << (2 * skip_levels);
	const auto result = target_level_start + level_offset * descendants_per_node;
	return result;
}
*/

} // RGL
