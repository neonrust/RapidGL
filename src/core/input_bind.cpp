#include "input_bind.h"

#include <format>
#include <string_view>
#include <cassert>

using namespace std::literals;

namespace RGL
{

std::string ActionBind::description() const
{
	switch(_type)
	{
	case Key:
		// TODO: better string representation for the key
		return std::format("<action key=\"{}\">"sv, int(_key));
	case MouseBtn:
	{
		static const std::string_view names[] = { "left"sv, "right"sv, "middle"sv, "side"sv, "extra"sv, "forward"sv, "back"sv };
		return std::format("<action mouse button=\"{}\">"sv, names[_mouseBtn]);
	}
	case ControllerBtn:
		// TODO: better string representation for the button
		return std::format("<action controller button=\"{}\">"sv, _controllerIdx, int(_controllerBtn));
		break;
	}
	return "<action invalid>";
}

// ----------------------------------------------------------------------------

KeyCode ActionBind::key() const
{
	assert(_type == Key);
	return _key;
}

// ----------------------------------------------------------------------------

MouseButton ActionBind::button() const
{
	assert(_type == MouseBtn);
	return _mouseBtn;
}

// ----------------------------------------------------------------------------

std::pair<size_t, ControllerButton> ActionBind::controllerButton() const
{
	assert(_type == ControllerBtn);
	return { _controllerIdx, _controllerBtn };
}

// ----------------------------------------------------------------------------

ActionBind::TriggerState ActionBind::triggered() const
{
	const auto prev_active = _previousState;
	const auto active_now = check_state();
	_previousState = active_now;

	switch(_trigger)
	{
	case Press:
		return active_now and not prev_active ? Triggered : NotTriggered;
	case Hold:
		if(active_now)
			return prev_active ? TriggerStill : Triggered;
		else
			return prev_active ? TriggerEnd : NotTriggered;
	case Repeat:
		// TODO
		return NotTriggered;//IsKeyPressedRepeat(_key) ? Triggered : NotTriggered;
	case Release:
		return not active_now and prev_active ? Triggered : NotTriggered;;
	}
	return NotTriggered;
}

} // RGL
