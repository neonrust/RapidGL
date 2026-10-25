#include "input_bind.h"

#include <format>
#include <string_view>
#include <cassert>

using namespace std::literals;

namespace RGL
{

ActionBind::ActionBind()
{
}

// ----------------------------------------------------------------------------

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

bool ActionBind::current_state() const
{
	switch(_button.index())
	{
	case 0:
		return Input::isKeyDown(std::get<KeyCode>(_button));
	case 1:
		return Input::isMouseDown(std::get<MouseButton>(_button));
	case 2:
	{
		const auto &def = std::get<ControllerDef>(_button);
		return Input::isControllerButtonDown(def.idx, def.button);
	}
	}

	return false;
}

// ----------------------------------------------------------------------------

bool ActionBind::previous_state() const
{
	switch(_button.index())
	{
	case 0:
		return Input::wasKeyDown(std::get<KeyCode>(_button));
	case 1:
		return Input::wasMouseDown(std::get<MouseButton>(_button));
	case 2:
	{
		const auto &def = std::get<ControllerDef>(_button);
		return Input::wasControllerButtonDown(def.idx, def.button);
	}
	}

	return false;
}

// ----------------------------------------------------------------------------

ActionBind::operator bool() const
{
	return triggered() != TriggerState::NotTriggered;
}

// ----------------------------------------------------------------------------

ActionBind::TriggerState ActionBind::triggered() const
{
	const auto active_now = current_state();
	const auto was_active = previous_state();

	switch(_trigger)
	{
	case Press:
		return active_now and not was_active ? Triggered : NotTriggered;
	case Hold:
		if(active_now)
			return was_active ? TriggerStill : Triggered;
		else
			return was_active ? TriggerEnd : NotTriggered;
	case Repeat:
		// TODO
		return NotTriggered;//IsKeyPressedRepeat(_key) ? Triggered : NotTriggered;
	case Release:
		return not active_now and was_active ? Triggered : NotTriggered;;
	}
	return NotTriggered;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

AxisBind::AxisBind()
{
}

// ----------------------------------------------------------------------------

AxisBind::AxisBind(MouseAxis axis, uint_fast16_t rangeMax, float threshold, ThresholdDirection dir) :
	_axis(MouseDef{ axis, float(rangeMax) })
{
	setThreshold(threshold, dir);
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

		const auto pos = Input::mousePosition();
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

	std::unreachable();
}

// ----------------------------------------------------------------------------

int_fast16_t AxisBind::rawValue() const
{
	assert(_axis.index() < 2);

	if(_axis.index() == 0)
	{
		const auto &def = std::get<MouseDef>(_axis);

		const auto pos = Input::mousePosition();
		if(def.axis == MouseAxis::Horizontal)
			return int_fast16_t(pos.x);
		else
			return int_fast16_t(pos.y);
	}
	else if(_axis.index() == 1)
	{
		const auto &def = std::get<ControllerDef>(_axis);

		return int_fast16_t(Input::controllerAxis(def.idx, int(def.axis))*32767);
	}

	std::unreachable();
}

// ----------------------------------------------------------------------------

bool AxisBind::triggered() const
{
	if(_thresholdDirection == ThresholdDirection::Greater)
		return value() >= _threshold;
	return value() <= _threshold;
}

// ----------------------------------------------------------------------------

AxisBind::operator bool() const
{
	return triggered();
}

} // RGL
