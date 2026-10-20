#include "strain/CrawlerController.h"

#include "strain/raymath-wrap.h"

#include <numbers>

using namespace std::chrono;
using namespace std::literals;


const float s_mouselook_max_yaw = 0.9f;
const float s_mouselook_max_pitch = 0.7f;

static const auto s_move_duration { 400ms };
static const auto s_turn_duration { 250ms };

[[maybe_unused]] static constexpr auto AXIS_Z   = Vector3 { 0, 0, 1 };
[[maybe_unused]] static constexpr auto AXIS_X   = Vector3 { 1, 0, 0 };

static constexpr WorldDist s_camera_facing_dir[] = { // forward vector
	{  0,  1,  0 }, // DIR_NORTH
	{  1,  0,  0 }, // DIR_EAST
	{  0, -1,  0 }, // DIR_SOUTH
	{ -1,  0,  0 }, // DIR_WEST
};

// ----------------------------------------------------------------------------

CrawlerController::CrawlerController(int screenWidth, int screenHeight) :
	_camera({
		.position = { 0, 0, 0 },
		.target = { 0, 1, 0 },
		.up = AXIS_Z,
		.fovy = 65,
		.projection = CAMERA_PERSPECTIVE,
	}),
	_mouseScaleYaw(0.05f),
	_mouseScalePitch(0.03f),
	_screenCenter({ float(screenWidth/2), float(screenHeight/2) }),
	_grid_position({ 0, 0, 0 }),
	_facing(DIR_NORTH)
{
}

// ----------------------------------------------------------------------------

void CrawlerController::update(std::chrono::nanoseconds frameTime)
{
	if(_moveCommand == None and not _mouseLooking)
	{
		if(_binds.forward and _binds.forward.triggered())
			_moveCommand = Forward;
		else if(_binds.back and _binds.back.triggered())
			_moveCommand = Backward;
		else if(_binds.left and _binds.left.triggered())
			_moveCommand = Left;
		else if(_binds.right and _binds.right.triggered())
			_moveCommand = Right;
		else if(_binds.turnLeft and _binds.turnLeft.triggered())
			_moveCommand = TurnLeft;
		else if(_binds.turnRight and _binds.turnRight.triggered())
			_moveCommand = TurnRight;
	}

	if(_animation.type == Animation::None and not _mouseLooking)
	{
		// camera_animation_end = now; // if no move was made

		if(_moveCommand == None and _binds.mouseLook and _binds.mouseLook.triggered())
		{
			if(not _mouseLooking)
			{
				_mouseLooking = true;
				_mouseLookGrab = GetMousePosition();
				HideCursor();
				SetMousePosition(int(_screenCenter.x), int(_screenCenter.y));
				_mouseLookYaw = 0;
				_mouseLookPitch = 0;

				// std::cout << "mouselook START @ " << _mouseLookGrab << '\n';
			}
		}

		if(_moveCommand == None)
			_animation.type = Animation::None;

		else if(_moveCommand == TurnLeft or _moveCommand == TurnRight)
		{
			_animation.type = _moveCommand == TurnRight? Animation::TurnRight: Animation::TurnLeft;
			// camera_animation_duration = turn_duration;
			_animation.duration = s_turn_duration;
			_animation.elapsed = 0s;

			_animation.facing.prev = _facing;

			const auto left = _moveCommand == TurnLeft;
			_animation.facing.next = direction_facing_t((int(DIR_COUNT + _facing) + (left? -1 : 1)) % DIR_COUNT);

			_facing = _animation.facing.next; // NOTE: new direction is set immediately

			// std::cout << "start turn animation: "
			// 		  << _animation.facing.prev << " -> " << _animation.facing.next
			// 		  << " // "
			// 		  << s_camera_facing_pos[_animation.facing.prev] << " -> " << s_camera_facing_pos[_animation.facing.next]
			// 		  << "; in " << duration_cast<milliseconds>(_animation.duration) << '\n';
		}
		else // _moveCommand is a move
		{
			const auto desired_position = _grid_position + s_camera_facing_dir[(_facing + int(_moveCommand) - int(Forward)) % DIR_COUNT];
			if(_queryWalkable and not _queryWalkable(desired_position))
			{
				// TODO: emit a "can't move" event (for sound and visual cues)
				// TODO: prevent spam for reaching here...
				//   it will be called each frame while the move key is being held
				//   ideally, ignore until key is released

				// std::cout << "move " << desired_position << " DENIED!\n";
				_moveCommand = None;
				return;
			}

			_animation.position.prev = _grid_position;
			_animation.position.next = desired_position;
			_grid_position = desired_position; // NOTE: new position is set immediately

			_animation.type = Animation::Move;
			_animation.duration = s_move_duration;
			_animation.elapsed = 0s;

			// std::cout << "start move animation:"
			// 		  << _animation.position.prev << " -> " << _animation.position.next
			// 		  << "; in " << duration_cast<milliseconds>(_animation.duration) << '\n';
		}
	}


	if(_animation.type != Animation::None)
	{
		_animation.elapsed += frameTime;
		const float animation_progress = std::min(1.f, float(duration_cast<milliseconds>(_animation.elapsed).count())/float(duration_cast<milliseconds>(_animation.duration).count()));
		const bool done = animation_progress == 1.f;

		if(not done)
		{
			if(_animation.type == Animation::Move)
			{
				_camera.position = Vector3Lerp(_animation.position.prev.world(GRID_SIZE), _animation.position.next.world(GRID_SIZE), animation_progress);
				_camera.target = Vector3Add(_camera.position, s_camera_facing_dir[_facing] * GRID_SIZE);
				// TODO: add camera bobbing

				// std::cout << "progress: " << std::fixed << std::setprecision(1) << animation_progress << ": "
				// 		  << _camera.position << ' ' << _animation.position.next << '\n';
			}
			else // TurnLeft or TurnRight
			{
				const auto &facing_from = s_camera_facing_dir[_animation.facing.prev];
				// const auto &facing_to = camera_facing_vector[camera_animation_next_facing];

				const auto right = _animation.type == Animation::TurnRight;
				const auto angle = std::numbers::pi_v<float>/2*animation_progress;
				const auto facing = Vector3RotateByAxisAngle(facing_from * GRID_SIZE, AXIS_Z, right? -angle: angle);
				_camera.target = Vector3Add(_camera.position, facing);

				// TODO: "lean" camera left/right to fake inertia?

				// std::cout << "progress: " << std::fixed << std::setprecision(1) << animation_progress << ": "
				// 		  << facing << '\n';
			}

			if(animation_progress >= 0.5f) // allow next movement input halfway through current animation
				_moveCommand = None;
		}
		else
		{
			if(_animation.type == Animation::Move)
			{
				_grid_position = _animation.position.next; // though, this should already be set when animation started
				_camera.position = _grid_position.world(GRID_SIZE);
				// std::cout << "DONE " << _position << " -> " << _camera.position << '\n';
			}
			else // TurnLeft or TurnRight
			{
				_facing = _animation.facing.next;
				// std::cout << "DONE " << _facing << " -> " << s_camera_facing_pos[_facing] << '\n';
			}

			_camera.target = Vector3Add(_camera.position, s_camera_facing_dir[_facing] * GRID_SIZE);

			_animation.type = Animation::None;
		}
	}
	else if(_mouseLooking)
	{
		if(_binds.mouseLook and not _binds.mouseLook.triggered())
		{
			_mouseLooking = false;
			// return the cursor to where we started
			SetMousePosition(int(_mouseLookGrab.x), int(_mouseLookGrab.y));
			ShowCursor();

			_camera.target = Vector3Add(_camera.position, s_camera_facing_dir[_facing] * GRID_SIZE);

			// std::cout << "mouselook END\n";

			// TODO: releasing while looking far left/right should snap facing to nearest direction
			//  also the "snap" shouldn't be instant, but instead a "very quick" animation, lika 20% of anormal turn tine
		}
		else
		{
			const auto currPos = GetMousePosition();
			SetMousePosition(int(_screenCenter.x), int(_screenCenter.y));

			const auto mouse_offset = Vector2Subtract(currPos, _screenCenter);
			const auto yawAdd = mouse_offset.x*_mouseScaleYaw;
			const auto pitchAdd = mouse_offset.y*_mouseScalePitch;

			_mouseLookYaw = clamp(_mouseLookYaw + yawAdd, -s_mouselook_max_yaw, s_mouselook_max_yaw);
			_mouseLookPitch = clamp(_mouseLookPitch + pitchAdd, -s_mouselook_max_pitch, s_mouselook_max_pitch);

			auto target = s_camera_facing_dir[_facing] * GRID_SIZE; // default target for this facing direction
			target = Vector3RotateByAxisAngle(target, AXIS_Z, -std::numbers::pi_v<float>/2*_mouseLookYaw);
			const auto rightVector = Vector3CrossProduct(AXIS_Z, target);
			target = Vector3RotateByAxisAngle(target, rightVector, std::numbers::pi_v<float>/2*_mouseLookPitch);

			_camera.target = Vector3Add(_camera.position, target);
		}
	}
}

// ----------------------------------------------------------------------------

void CrawlerController::setMouseSensitivity(float yaw, float pitch)
{
	_mouseScaleYaw = yaw / (_screenCenter.x*2);
	_mouseScalePitch = pitch / (_screenCenter.y*2);
}

// ----------------------------------------------------------------------------

move_direction_t CrawlerController::wantsToMove(move_direction_t directions) const
{
	switch(_moveCommand)
	{
	case TurnLeft:
		return directions & TURN_LEFT;
	case TurnRight:
		return directions & TURN_RIGHT;
	case Forward:
		return directions & MOVE_FORWARD;
	case Backward:
		return directions & MOVE_BACKWARD;
	case Left:
		return directions & MOVE_LEFT;
	case Right:
		return directions & MOVE_RIGHT;
	default:
		return MOVE_NONE;
	}
}

// ----------------------------------------------------------------------------

void CrawlerController::invalidate()
{
	_camera.position.x = float(_grid_position.x) * GRID_SIZE;
	_camera.position.y = float(_grid_position.y) * GRID_SIZE;
	_camera.position.z = float(_grid_position.z) * GRID_SIZE;

	_camera.target = Vector3Add(_camera.position, s_camera_facing_dir[_facing] * GRID_SIZE);
}

// ----------------------------------------------------------------------------
