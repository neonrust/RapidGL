#pragma once

#include <functional>
#include <chrono>

#include "raylib-wrap.h"

#include "types.h"
#include "common.h"
#include "InputBind.h"


class CrawlerController
{
public:
	CrawlerController(int screenWidth, int screenHeight);

	void update(std::chrono::nanoseconds frameTime);

	inline void setWalkableQuery(std::function<bool(GridPos)> callback) { _queryWalkable = callback; }

	// TODO: de-couple bind config from this class? e.g. the crawler could register its "commands" with a global registry)
	void setForward(ActionBind bind)   { _binds.forward   = bind; }
	void setBack(ActionBind bind)      { _binds.back      = bind; }
	void setLeft(ActionBind bind)      { _binds.left      = bind; }
	void setRight(ActionBind bind)     { _binds.right     = bind; }
	void setTurnLeft(ActionBind bind)  { _binds.turnLeft  = bind; }
	void setTurnRight(ActionBind bind) { _binds.turnRight = bind; }
	void setMouseLook(ActionBind bind) { _binds.mouseLook = bind; }

	void setMouseSensitivity(float yaw, float pitch);

	// push the crawler in some direction (meaning a quick movement that the player can't control)
	void pushX(int offset) { (void)offset; };
	void pushY(int offset) { (void)offset; };

	inline void setPosition(const GridPos &p) { _grid_position = p; invalidate(); };
	inline void setFacing(direction_facing_t facing) { _facing = facing; invalidate(); }

	inline const GridPos &grid() const { return _grid_position; }
	inline direction_facing_t facing() const { return _facing; }

	inline const Camera3D &camera() const { return _camera; }

	move_direction_t wantsToMove(move_direction_t directions) const;

private:
	void invalidate();

private:
	Camera3D _camera;

	bool _mouseLooking { false };
	float _mouseScaleYaw;
	float _mouseScalePitch;

	enum MoveCommand : int { None, Forward, Right, Backward, Left, TurnLeft, TurnRight };
	MoveCommand _moveCommand { None };

	enum class Animation : int { None, Move, TurnLeft, TurnRight, Push };
	struct
	{
		Animation type { None };

		std::chrono::nanoseconds duration { 0 };
		std::chrono::nanoseconds elapsed { 0 };

		struct
		{
			GridPos prev;
			GridPos next;
		} position;
		struct
		{
			direction_facing_t prev;
			direction_facing_t next;
		} facing;
	} _animation;

	struct {
		ActionBind forward;
		ActionBind back;
		ActionBind left;
		ActionBind right;
		ActionBind turnLeft;
		ActionBind turnRight;
		ActionBind mouseLook;
	} _binds;

	Vector2 _mouseLookGrab;
	const Vector2 _screenCenter;
	float _mouseLookYaw;
	float _mouseLookPitch;

	std::function<bool(GridPos)> _queryWalkable;

	GridPos _grid_position;
	direction_facing_t _facing { DIR_NORTH };

};
