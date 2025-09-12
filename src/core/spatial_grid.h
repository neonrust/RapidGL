#pragma once

#include "container_types.h"
#include <glm/vec3.hpp>

#include <array>

namespace RGL
{

/*
 TODO: interface ?
template<typename IdT=uint32_t, typename PosT=glm::vec3>
class SpatialPartitioning
{
public:
	virtual void add(IdT id, const PosT &position, float radius) = 0;
	virtual void remove(IdT id) = 0;
	virtual void move(IdT id, const PosT &position) = 0;
};
*/

template<typename IdT=uint32_t, typename PosT=glm::vec3, typename AxisT=int64_t>
class SpatialGrid //: SpatialPartitioning<IdT, PosT>
{
public:
	struct GridCoord
	{
		AxisT x;
		AxisT y;
		AxisT z;
	};
	static constexpr AxisT NO_COORD = std::numeric_limits<AxisT>::max();
	using Cells = std::array<GridCoord, 8>;

public:
	SpatialGrid(AxisT width, AxisT depth, AxisT height=NO_COORD, const PosT &origin={0, 0, 0});  // symmetric around (0, 0) ?

	void  add(IdT id, const PosT &position, PosT::value_type radius);
	bool  remove(IdT id);
	void  move(IdT id, const PosT &position);

	Cells get_cells(IdT id) const;

	// TODO: queries:
	//   "all within radius from position"
	//   "all in cell"
	//  other useful stuff?

private:
	inline GridCoord to_grid_pos(const glm::vec3 &pos) const
	{
		const auto grid_space = (pos - _origin) / _size;
		return {
			AxisT(grid_space.x),
			AxisT(grid_space.y),
			AxisT(grid_space.z),
		};
	}

	struct Object
	{
		PosT positino;
		PosT::value_type radius;

		std::array<GridCoord, 8> cells;
		size_t num_cells;
	};

private:
	PosT _size;
	PosT _origin;

	dense_map<IdT, Object> _objects;
};

} // RGL
