#include "CrawlerController.h"

#include "formatters_glm.h"  // IWYU pragma: keep
#include "constants.h"

#include <input.h>
#include <numbers>

using namespace std::chrono;
using namespace std::literals;

template<>
struct std::formatter<RGL::GridPos>
{
	std::formatter<decltype(RGL::GridPos::x)> elem_fmt;

	constexpr auto parse(std::format_parse_context& ctx)
	{
		return elem_fmt.parse(ctx);
	}

	auto format(const RGL::GridPos &g,
				std::format_context& ctx) const
	{
		auto out_iter = ctx.out();
		*out_iter++ = '{';
		out_iter = elem_fmt.format(g.x, ctx);
		*out_iter++ = ';';
		out_iter = elem_fmt.format(g.y, ctx);
		*out_iter++ = ';';
		out_iter = elem_fmt.format(g.z, ctx);
		*out_iter++ = '}';
		return out_iter;
	}
};


const float s_mouselook_max_yaw = 0.9f;
const float s_mouselook_max_pitch = 0.7f;

static const auto s_move_duration { 400ms };
static const auto s_turn_duration { 250ms };

using GridDistance = RGL::GridPos;
static constexpr GridDistance s_camera_facing_dir[] = { // forward vector
	{  0,  1,  0 }, // Movement::North
	{  1,  0,  0 }, // Movement::East
	{  0, -1,  0 }, // Movement::South
	{ -1,  0,  0 }, // Movement::West
};

// ----------------------------------------------------------------------------

CrawlerController::CrawlerController(const glm::uvec2 &grabTopLeft, const glm::uvec2 &grabSize, float grid_size) :
	_grid_size(grid_size),
	_mouseScaleYaw(0.05f),
	_mouseScalePitch(0.03f),
	_grabTopLeft(grabTopLeft),
	_grabSize(grabSize)
{
}

// ----------------------------------------------------------------------------

void CrawlerController::update(std::chrono::nanoseconds frameTime)
{
	if(_moveCommand == MoveCommand::None and not _mouseLooking)
	{
		// in theory, '_moveCommand' could be multiplke moves at once,
		// but now we're just setting one

		if(_binds.forward and _binds.forward.triggered())
			_moveCommand = MoveCommand::Forward;
		else if(_binds.back and _binds.back.triggered())
			_moveCommand = MoveCommand::Backward;
		else if(_binds.left and _binds.left.triggered())
			_moveCommand = MoveCommand::Left;
		else if(_binds.right and _binds.right.triggered())
			_moveCommand = MoveCommand::Right;
		else if(_binds.turnLeft and _binds.turnLeft.triggered())
			_moveCommand = MoveCommand::TurnLeft;
		else if(_binds.turnRight and _binds.turnRight.triggered())
			_moveCommand = MoveCommand::TurnRight;
	}

	if(_animation.type == GridAnimation::None and not _mouseLooking)
	{
		// camera_animation_end = now; // if no move was made

		if(_moveCommand == MoveCommand::None and _binds.mouseLook and _binds.mouseLook.triggered())
		{
			if(not _mouseLooking)
			{
				_mouseLooking = true;
				_mouseLookGrab = RGL::Input::mousePosition();
				RGL::Input::setMouseCursorVisible(false);
				RGL::Input::setMousePosition(_grabTopLeft + _grabSize/2u);
				_mouseLookYaw = 0;
				_mouseLookPitch = 0;

				Log::debug("mouselook START @ {}", _mouseLookGrab);
			}
		}

		if(_moveCommand == MoveCommand::None)
			_animation.type = GridAnimation::None;

		else if(_moveCommand == MoveCommand::TurnLeft or _moveCommand == MoveCommand::TurnRight)
		{
			const auto left = _moveCommand == MoveCommand::TurnLeft;

			_animation.type = (not left)? GridAnimation::TurnRight: GridAnimation::TurnLeft;
			_animation.duration = s_turn_duration;
			_animation.elapsed = 0s;

			_animation.facing.prev = _facing;

			if(left)
				_animation.facing.next = _facing == Facing::North? Facing::West: Facing((int(_facing) - 1));
			else
				_animation.facing.next = Facing((uint32_t(_facing) + 1) % FacingCount);

			_facing = _animation.facing.next; // NOTE: new direction is set immediately

			// Log::debug("start turn animation: {} -> {} // {} -> {}; in {}",
			// 		   _animation.facing.prev,
			// 		   _animation.facing.next,
			// 		   s_camera_facing_pos[_animation.facing.prev],
			// 		   s_camera_facing_pos[_animation.facing.next],
			// 		   duration_cast<milliseconds>(_animation.duration));
		}
		else // _moveCommand is a move
		{
			const auto desired_position = _grid_position + s_camera_facing_dir[uint32_t(int(FacingCount) + int(_facing) + int(_moveCommand) - int(MoveCommand::Forward)) % FacingCount];
			if(_queryWalkable and not _queryWalkable(desired_position))
			{
				// TODO: emit a "can't move" event (for sound and visual cues)
				// TODO: prevent spam for reaching here...
				//   it will be called each frame while the move key is being held
				//   probably, just ignore until key is released

				// Log::debug("move {} DENIED", desired_position);
				_moveCommand = MoveCommand::None;
				return;
			}

			_animation.position.prev = _grid_position;
			_animation.position.next = desired_position;
			_grid_position = desired_position; // NOTE: new position is set immediately

			_animation.type = GridAnimation::Move;
			_animation.duration = s_move_duration;
			_animation.elapsed = 0s;

			// Log::debug("start move animation:{} -> {}; in {}",
			// 		   _animation.position.prev,
			// 		   _animation.position.next,
			// 		   duration_cast<milliseconds>(_animation.duration));
		}
	}


	if(_animation.type != GridAnimation::None)
	{
		_animation.elapsed += frameTime;
		const float animation_progress = std::min(1.f, float(duration_cast<milliseconds>(_animation.elapsed).count())/float(duration_cast<milliseconds>(_animation.duration).count()));
		const bool done = animation_progress == 1.f;

		if(not done)
		{
			if(_animation.type == GridAnimation::Move)
			{
				_camera.setPosition(glm::lerp(_animation.position.prev * _grid_size, _animation.position.next * _grid_size, animation_progress));
				_camera.lookAt(_camera.position() + s_camera_facing_dir[uint_fast8_t(_facing)] * _grid_size);
				// TODO: add camera bobbing

				// std::cout << "progress: " << std::fixed << std::setprecision(1) << animation_progress << ": "
				// 		  << _camera.position << ' ' << _animation.position.next << '\n';
			}
			else // TurnLeft or TurnRight
			{
				const auto &facing_from = s_camera_facing_dir[uint_fast8_t(_animation.facing.prev)];
				// const auto &facing_to = camera_facing_vector[camera_animation_next_facing];

				const auto right = _animation.type == GridAnimation::TurnRight;
				const auto angle = std::numbers::pi_v<float>/2*animation_progress;
				// const auto facing = Vector3RotateByAxisAngle(facing_from * _grid_size, AXIS_Z, right? -angle: angle);
				const auto facingMatrix = glm::rotate(glm::mat4(1), right? -angle: angle, AXIS_Z);
				const auto facing = glm::vec3(facingMatrix * glm::vec4(facing_from * _grid_size, 1.f));
				_camera.lookAt(_camera.position() + facing);

				// TODO: "lean" camera left/right to fake inertia?

				// std::cout << "progress: " << std::fixed << std::setprecision(1) << animation_progress << ": "
				// 		  << facing << '\n';
			}

			if(animation_progress >= 0.5f) // allow next movement input halfway through current animation
				_moveCommand = MoveCommand::None;
		}
		else
		{
			if(_animation.type == GridAnimation::Move)
			{
				_grid_position = _animation.position.next; // though, this should already be set when animation started
				_camera.setPosition(_grid_position * _grid_size);
				// std::cout << "DONE " << _position << " -> " << _camera.position << '\n';
			}
			else // TurnLeft or TurnRight
			{
				_facing = _animation.facing.next;
				// std::cout << "DONE " << _facing << " -> " << s_camera_facing_pos[_facing] << '\n';
			}

			_camera.lookAt(_camera.position() + s_camera_facing_dir[uint_fast8_t(_facing)] * _grid_size);

			_animation.type = GridAnimation::None;
		}
	}
	else if(_mouseLooking)
	{
		if(_binds.mouseLook and not _binds.mouseLook.triggered())
		{
			_mouseLooking = false;
			// return the cursor to where we started
			RGL::Input::setMousePosition(_mouseLookGrab);
			RGL::Input::setMouseCursorVisible(true);

			_camera.lookAt(_camera.position() + s_camera_facing_dir[uint_fast8_t(_facing)] * _grid_size);

			// std::cout << "mouselook END\n";

			// TODO: releasing while looking far left/right should snap facing to nearest direction
			//  also the "snap" shouldn't be instant, but instead a "very quick" animation, lika 20% of anormal turn tine
		}
		else
		{
			const auto currPos = RGL::Input::mousePosition();
			const auto dragCenter = _grabTopLeft + _grabSize/2u;
			RGL::Input::setMousePosition(dragCenter);

			const auto mouse_offset = glm::vec2(currPos) - glm::vec2(dragCenter);
			const auto yawAdd = mouse_offset.x*_mouseScaleYaw;
			const auto pitchAdd = mouse_offset.y*_mouseScalePitch;

			_mouseLookYaw   = glm::clamp(_mouseLookYaw   + yawAdd,   -s_mouselook_max_yaw,   s_mouselook_max_yaw);
			_mouseLookPitch = glm::clamp(_mouseLookPitch + pitchAdd, -s_mouselook_max_pitch, s_mouselook_max_pitch);

			auto target = s_camera_facing_dir[uint_fast8_t(_facing)] * _grid_size; // default target for this facing direction
			// target = Vector3RotateByAxisAngle(target, AXIS_Z, -std::numbers::pi_v<float>/2*_mouseLookYaw);
			auto targetMatrix = glm::rotate(glm::mat4(1), -std::numbers::pi_v<float>/2*_mouseLookYaw, AXIS_Z);
			target = glm::vec3(targetMatrix * glm::vec4(target, 1.f));
			const auto rightVector = glm::cross(AXIS_Z, target);
			// target = Vector3RotateByAxisAngle(target, rightVector, std::numbers::pi_v<float>/2*_mouseLookPitch);
			targetMatrix = glm::rotate(glm::mat4(1), std::numbers::pi_v<float>/2*_mouseLookPitch, rightVector);
			target = glm::vec3(targetMatrix * glm::vec4(target, 1.f));

			_camera.lookAt(_camera.position() + target);
		}
	}
}

// ----------------------------------------------------------------------------

void CrawlerController::setMouseSensitivity(float yaw, float pitch)
{
	_mouseScaleYaw   =  yaw  / float(_grabSize.x*2);
	_mouseScalePitch = pitch / float(_grabSize.y*2);
}

// ----------------------------------------------------------------------------

CrawlerController::Movement CrawlerController::wantsToMove(Movement directions) const
{
	switch(_moveCommand)
	{
	case MoveCommand::TurnLeft:
		return Movement(uint_fast8_t(directions) & uint_fast8_t(Movement::TurnLeft));
	case MoveCommand::TurnRight:
		return Movement(int_fast8_t(directions) & uint_fast8_t(Movement::TurnRight));
	case MoveCommand::Forward:
		return Movement(uint_fast8_t(directions) & uint_fast8_t(Movement::Forward));
	case MoveCommand::Backward:
		return Movement(uint_fast8_t(directions) & uint_fast8_t(Movement::Backward));
	case MoveCommand::Left:
		return Movement(uint_fast8_t(directions) & uint_fast8_t(Movement::Left));
	case MoveCommand::Right:
		return Movement(uint_fast8_t(directions) & uint_fast8_t(Movement::Right));
	default:
		return Movement::None;
	}
}

// ----------------------------------------------------------------------------

void CrawlerController::invalidate()
{
	_camera.setPosition({ float(_grid_position.x) * _grid_size,
						  float(_grid_position.y) * _grid_size,
						  float(_grid_position.z) * _grid_size });

	_camera.lookAt(_camera.position() + s_camera_facing_dir[uint_fast8_t(_facing)] * _grid_size);
}

// ----------------------------------------------------------------------------
