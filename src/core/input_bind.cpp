#include "input_bind.h"

#include <format>
#include <string_view>
#include <cassert>

using namespace std::literals;

namespace RGL
{

ActionBind::ActionBind(KeyCode key, Trigger trigger) :
	_trigger(trigger),
	_button(key)
{
}

// ----------------------------------------------------------------------------

ActionBind::ActionBind(MouseButton button, Trigger trigger) :
	_trigger(trigger),
	_button(button)
{
}

// ----------------------------------------------------------------------------

ActionBind::ActionBind(int controllerIdx, ControllerButton button, Trigger trigger) :
	_trigger(trigger),
	_button(ControllerDef{ button ,controllerIdx })
{
}

// ----------------------------------------------------------------------------

std::string ActionBind::description() const
{
	switch(_button.index())
	{
	case 0:
		// TODO: better string representation for the key
		return std::format("<action key=\"{}\">"sv, int(std::get<KeyCode>(_button)));
	case 1:
	{
		static const std::string_view names[] = { "left"sv, "right"sv, "middle"sv, "side"sv, "extra"sv, "forward"sv, "back"sv };
		return std::format("<action mouse button=\"{}\">"sv, names[std::get<MouseButton>(_button)]);
	}
	case 2:
		// TODO: better string representation for the button
		const auto &def = std::get<ControllerDef>(_button);
		return std::format("<action controller button=\"{}\">"sv, def.idx, int(def.button));
		break;
	}
	return "<action invalid>";
}

// ----------------------------------------------------------------------------

KeyCode ActionBind::key() const
{
	assert(std::holds_alternative<KeyCode>(_button));
	return std::get<KeyCode>(_button);
}

// ----------------------------------------------------------------------------

MouseButton ActionBind::button() const
{
	assert(std::holds_alternative<MouseButton>(_button));
	return std::get<MouseButton>(_button);
}

// ----------------------------------------------------------------------------

std::pair<size_t, ControllerButton> ActionBind::controllerButton() const
{
	assert(std::holds_alternative<ControllerDef>(_button));
	const auto &def = std::get<ControllerDef>(_button);
	return { def.idx, def.button };
}

// ----------------------------------------------------------------------------

bool RGL::ActionBind::check_state() const
{
	switch(_button.index())
	{
	case 0:
		return Input::wasKeyPressed(std::get<KeyCode>(_button));
	case 1:
		return Input::wasMousePressed(KeyCode(std::get<MouseButton>(_button)));
	case 2:
	{
		const auto &def = std::get<ControllerDef>(_button);
		return Input::controllerButtonDown(def.idx, int(def.button));
	}
	}

	return false;
}

// ----------------------------------------------------------------------------

ActionBind::operator bool() const
{
	return not (std::holds_alternative<KeyCode>(_button) and std::get<KeyCode>(_button) == KeyCode::None);
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

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

AxisBind::AxisBind(MouseAxis axis, uint_fast16_t rangeMax) :
	_axis(MouseDef{ axis, float(rangeMax) })
{
}

// ----------------------------------------------------------------------------

AxisBind::AxisBind(MouseAxis axis, uint_fast16_t rangeMax, float threshold, ThresholdDirection dir) :
	_axis(MouseDef{ axis, float(rangeMax) })
{
	setThreshold(threshold, dir);
}

// ----------------------------------------------------------------------------

AxisBind::AxisBind(int controllerIdx, ControllerAxis axis) :
	_axis(ControllerDef{ axis, controllerIdx })
{
}

// ----------------------------------------------------------------------------

AxisBind::AxisBind(int controllerIdx, ControllerAxis axis, float threshold, ThresholdDirection dir) :
	_axis(ControllerDef{ axis, controllerIdx })
{
	setThreshold(threshold, dir);
}

// ----------------------------------------------------------------------------

float AxisBind::value() const
{
	assert(_axis.index() < 2);

	if(_axis.index() == 0)
	{
		const auto &def = std::get<MouseDef>(_axis);

		auto pos = Input::mousePosition();
		if(def.axis == MouseAxis::Horizontal)
			return float(pos.x) / def.max;
		else
			return float(pos.y) / def.max;
	}
	else if(_axis.index() == 1)
	{
		const auto &def = std::get<ControllerDef>(_axis);

		return Input::controllerAxis(def.idx, int(def.axis));
	}
}

} // RGL
