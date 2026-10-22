#pragma once

#include "camera.h"
#include "scene.h"

#include "input_bind.h"
#include <glm/vec2.hpp>

#include <functional>
#include <chrono>


class CrawlerController
{
public:
	enum class Facing : uint32_t { North, East, South, West };
	static constexpr uint32_t FacingCount = 4;
	// facing-relative movement; in theory, multiple at once is possible
	enum class Movement : uint_fast8_t {
		None      = 0x00,
		Forward   = 0x01,
		Right     = 0x02,
		Backward  = 0x04,
		Left      = 0x08,
		TurnLeft  = 0x10,
		TurnRight = 0x20,
	};

public:
	CrawlerController(const glm::uvec2 &grabTopLeft, const glm::uvec2 &grabSize, float grid_size=2.f);  // TODO: "free-look area"

	void update(std::chrono::nanoseconds frameTime);

	inline void setWalkableQuery(std::function<bool(RGL::GridPos)> callback) { _queryWalkable = callback; }

	// TODO: de-couple bind config from this class? e.g. the crawler could register its "commands" with a global registry)
	void setForward(RGL::ActionBind bind)   { _binds.forward   = bind; }
	void setBack(RGL::ActionBind bind)      { _binds.back      = bind; }
	void setLeft(RGL::ActionBind bind)      { _binds.left      = bind; }
	void setRight(RGL::ActionBind bind)     { _binds.right     = bind; }
	void setTurnLeft(RGL::ActionBind bind)  { _binds.turnLeft  = bind; }
	void setTurnRight(RGL::ActionBind bind) { _binds.turnRight = bind; }
	void setMouseLook(RGL::ActionBind bind) { _binds.mouseLook = bind; }

	void setMouseSensitivity(float yaw, float pitch);

	// push the crawler in some direction (meaning a quick movement that the player can't control)
	void pushX(int offset) { (void)offset; };
	void pushY(int offset) { (void)offset; };

	inline void setPosition(const RGL::GridPos &p) { _grid_position = p; invalidate(); };
	inline void setFacing(Facing facing) { _facing = facing; invalidate(); }

	inline const RGL::GridPos &grid() const { return _grid_position; }
	inline Facing facing() const { return _facing; }

	inline const RGL::Camera &camera() const { return _camera; }

	Movement wantsToMove(Movement directions) const;

private:
	void invalidate();

private:
	RGL::Camera _camera;
	float _grid_size;

	bool _mouseLooking { false };
	float _mouseScaleYaw;
	float _mouseScalePitch;

	enum class MoveCommand : int { None, Forward, Right, Backward, Left, TurnLeft, TurnRight };
	MoveCommand _moveCommand { MoveCommand::None };

	enum class GridAnimation : int { None, Move, TurnLeft, TurnRight, Push };
	struct
	{
		GridAnimation type { GridAnimation::None };

		std::chrono::nanoseconds duration { 0 };
		std::chrono::nanoseconds elapsed { 0 };

		struct
		{
			RGL::GridPos prev;
			RGL::GridPos next;
		} position;
		struct
		{
			Facing prev;
			Facing next;
		} facing;
	} _animation;

	struct {
		RGL::ActionBind forward;
		RGL::ActionBind back;
		RGL::ActionBind left;
		RGL::ActionBind right;
		RGL::ActionBind turnLeft;
		RGL::ActionBind turnRight;
		RGL::ActionBind mouseLook;
	} _binds;

	glm::uvec2 _mouseLookGrab;
	glm::uvec2 _grabTopLeft;
	glm::uvec2 _grabSize;
	float _mouseLookYaw;
	float _mouseLookPitch;

	std::function<bool(RGL::GridPos)> _queryWalkable;

	RGL::GridPos _grid_position { 0, 0 };
	Facing _facing { Facing::North };

};
