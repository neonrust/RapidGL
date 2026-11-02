#include "input_bind.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

#include <format>
#include <string_view>
#include <cassert>
#include "log.h"

using namespace std::literals;

namespace RGL
{

ActionBind::ActionBind()
{
}

// ----------------------------------------------------------------------------

ActionBind::ActionBind(KeyCode key, Trigger trigger)
{
	bind(key, trigger);
}

// ----------------------------------------------------------------------------

ActionBind::ActionBind(MouseButton button, Trigger trigger)
{
	bind(button, trigger);
}

// ----------------------------------------------------------------------------

ActionBind::ActionBind(int controllerIdx, ControllerButton button, Trigger trigger)
{
	bind(controllerIdx, button, trigger);
}

// ----------------------------------------------------------------------------

void ActionBind::bind(KeyCode key, Trigger trigger)
{
	_button = key;
	_trigger = trigger;
}

// ----------------------------------------------------------------------------

void ActionBind::bind(MouseButton button, Trigger trigger)
{
	_button = button;
	_trigger = trigger;
}

// ----------------------------------------------------------------------------

void ActionBind::bind(int controllerIdx, ControllerButton button, Trigger trigger)
{
	_button = ControllerDef{ button ,controllerIdx };
	_trigger = trigger;
}

// ----------------------------------------------------------------------------

std::string ActionBind::description() const
{
	switch(_button.index())
	{
	case 0:
		return std::format("<action>");
	case 1:
		// TODO: better string representation for the key
		return std::format("<action key=\"{}\">"sv, int(std::get<KeyCode>(_button)));
	case 2:
	{
		static const std::string_view names[] = { "left"sv, "right"sv, "middle"sv, "side"sv, "extra"sv, "forward"sv, "back"sv };
		return std::format("<action mouse button=\"{}\">"sv, names[std::get<MouseButton>(_button)]);
	}
	case 3:
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
		return false;
	case 1:
		return Input::isKeyDown(std::get<KeyCode>(_button));
	case 2:
		return Input::isMouseDown(std::get<MouseButton>(_button));
	case 3:
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
		return false;
	case 1:
		return Input::wasKeyDown(std::get<KeyCode>(_button));
	case 2:
		return Input::wasMouseDown(std::get<MouseButton>(_button));
	case 3:
	{
		const auto &def = std::get<ControllerDef>(_button);
		return Input::wasControllerButtonDown(def.idx, def.button);
	}
	}

	return false;
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

AxisBind1d &AxisBind::as_1d()
{
	assert(num_dimensions() == 1);
	return *static_cast<AxisBind1d *>(this);
}

// ----------------------------------------------------------------------------

AxisBind2d &AxisBind::as_2d()
{
	assert(num_dimensions() == 2);
	return *static_cast<AxisBind2d *>(this);
}

// ----------------------------------------------------------------------------

AxisBind1d::AxisBind1d() :
	AxisBind()
{
}

// ----------------------------------------------------------------------------

AxisBind1d::AxisBind1d(MouseAxis1d axis, uint_fast16_t rangeMax, float threshold, ThresholdDirection dir)
{
	bind(axis, rangeMax, threshold, dir);
}

// ----------------------------------------------------------------------------

AxisBind1d::AxisBind1d(int controllerIdx, ControllerAxis1d axis, float threshold, ThresholdDirection dir)
{
	bind(controllerIdx, axis, threshold, dir);
}

// ----------------------------------------------------------------------------

std::string AxisBind1d::description() const
{
	switch(_axis.index())
	{
	case 0:
		return std::format("<axis>");
	case 1:
		// TODO: better string representation for the axis
		return std::format("<axis mouse=\"{}\">"sv, int(std::get<MouseDef>(_axis).axis));
	case 2:
	{
		// TODO: better string representation for the axis
		const auto &def = std::get<ControllerDef>(_axis);
		return std::format("<axis controller=\"{}\" axis=\"{}\">"sv, def.idx, int(def.axis));
	}
	}
	return "<axis invalid>";
}

// ----------------------------------------------------------------------------

void AxisBind1d::bind(MouseAxis1d axis, uint_fast16_t rangeMax, float threshold, ThresholdDirection dir)
{
	_axis = MouseDef{ axis, float(rangeMax) };
	set_threshold(float(threshold) / float(rangeMax), dir);
}

// ----------------------------------------------------------------------------

void AxisBind1d::bind(int controllerIdx, ControllerAxis1d axis, float threshold, ThresholdDirection dir)
{
	_axis = ControllerDef{ axis, controllerIdx };
	set_threshold(threshold, dir);
}

// ----------------------------------------------------------------------------

float AxisBind1d::value() const
{
	assert(_axis.index() < 2);

	switch(_axis.index())
	{
	case 0:
		return 0.f;
	case 1:
	{
		const auto &def = std::get<MouseDef>(_axis);

		const auto pos = Input::mousePosition();

		if(not _infinite.enabled)
		{
			if(def.axis == MouseAxis1d::Horizontal)
				return float(pos.x) / def.max;
			return float(pos.y) / def.max;
		}

		switch(def.axis)
		{
		case MouseAxis1d::Horizontal:
			Input::setMousePosition({ def.max/2, pos.y });
			_infinite.value += _infinite.scaler * float(int32_t(pos.x) - int32_t(def.max/2)) / float(def.max);
			break;
		case MouseAxis1d::Vertical:
			Input::setMousePosition({ pos.x, def.max/2 });
			_infinite.value += _infinite.scaler * float(int32_t(pos.y) - int32_t(def.max/2)) / float(def.max);
			break;
		}

		if(_infinite.value <= _infinite.limits.min)
		{
			if(_infinite.limit_mode == LimitMode::Wrap)
				_infinite.value += _infinite.limits.max - _infinite.limits.min;
			else
				_infinite.value = _infinite.limits.min;
		}
		if(_infinite.value >= _infinite.limits.max)
		{
			if(_infinite.limit_mode == LimitMode::Wrap)
				_infinite.value -= _infinite.limits.max - _infinite.limits.min;
			else
				_infinite.value = _infinite.limits.max;
		}

		return _infinite.value;
	}
	case 2:
	{
		const auto &def = std::get<ControllerDef>(_axis);
		return Input::controllerAxis(def.idx, def.axis);
	}
	}

	std::unreachable();
}

// ----------------------------------------------------------------------------

int_fast16_t AxisBind1d::rawValue() const
{
	assert(_axis.index() >= 1 and _axis.index() <= 2);

	switch(_axis.index())
	{
	case 0:
		return 0;
	case 1:
	{
		const auto &def = std::get<MouseDef>(_axis);

		const auto pos = Input::mousePosition();
		if(def.axis == MouseAxis1d::Horizontal)
			return int_fast16_t(pos.x);
		return int_fast16_t(pos.y);
	}
	case 2:
		return int_fast16_t(value()*32767);
	}

	std::unreachable();
}

// ----------------------------------------------------------------------------

bool AxisBind1d::triggered() const
{
	if(_thresholdDirection == ThresholdDirection::Greater)
		return value() >= _threshold;
	return value() <= _threshold;
}

// ----------------------------------------------------------------------------

bool AxisBind1d::set_infinite(bool enabled)
{
	if(_axis.index() != 1) // only supported for mouse axis
		return false;

	if(_infinite.enabled == enabled) // already in expected mode
		return true;

	if(not Input::setMouseInfiniteMode(enabled))
		return false; // unsupported or already in infinite mode

	_infinite.enabled = enabled;
	if(enabled)
	{
		_infinite.value = 0.f;
		set_scaler(1.f);
		set_no_limits();
		Input::setMouseCursorVisible(false); // optional?
		const auto pos = Input::mousePosition();
		_infinite.grab_pos = pos;

		const auto &def = std::get<MouseDef>(_axis);
		switch(def.axis)
		{
		case MouseAxis1d::Horizontal: Input::setMousePosition({ def.max/2, pos.y }); break;
		case MouseAxis1d::Vertical:   Input::setMousePosition({ pos.x, def.max/2 }); break;
		}
	}
	else
	{
		Input::setMouseCursorVisible(true);
		Input::setMousePosition(_infinite.grab_pos);
	}

	return true;
}

// ----------------------------------------------------------------------------

bool AxisBind1d::set_infinite(bool enabled, float min, float max, LimitMode mode)
{
	auto ok = set_infinite(enabled);
	if(ok and enabled)
		set_limits(min, max, mode);
	return ok;
}

// ----------------------------------------------------------------------------

void AxisBind1d::set_scaler(float scaler)
{
	assert(scaler != 0);
	// TODO: support negative scaler
	_infinite.scaler = scaler;
}

// ----------------------------------------------------------------------------

void AxisBind1d::set_limits(float min, float max, LimitMode behaviour)
{
	assert(min < max);
	if(max < min)
		std::swap(max, min);
	if(min < max)
	{
		_infinite.limits = { min, max };
		_infinite.limit_mode = behaviour;
	}
}

// ----------------------------------------------------------------------------

void AxisBind1d::set_no_limits()
{
	_infinite.limits = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN() };
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

AxisBind2d::AxisBind2d() :
	AxisBind()
{
}

// ----------------------------------------------------------------------------

AxisBind2d::AxisBind2d(MouseAxis2d axis, glm::uvec2 rangeMax, glm::vec2 threshold, ThresholdDirection dir)
{
	bind(axis, rangeMax, threshold, dir);
}

// ----------------------------------------------------------------------------

AxisBind2d::AxisBind2d(int controllerIdx, ControllerAxis2d axis, glm::vec2 threshold, ThresholdDirection dir)
{
	bind(controllerIdx, axis, threshold, dir);
}

// ----------------------------------------------------------------------------

std::string AxisBind2d::description() const
{
	switch(_axis.index())
	{
	case 0:
		return std::format("<axis2d>");
	case 1:
		// TODO: better string representation for the axis
		return std::format("<axis2d mouse=\"{}\">"sv, int(std::get<MouseDef>(_axis).axis));
	case 2:
	{
		// TODO: better string representation for the axis
		const auto &def = std::get<ControllerDef>(_axis);
		return std::format("<axis2d controller=\"{}\" axis=\"{}\">"sv, def.idx, int(def.axis));
	}
	}
	return "<axis2d invalid>";
}


// ----------------------------------------------------------------------------

void AxisBind2d::bind(MouseAxis2d axis, glm::uvec2 rangeMax, glm::vec2 threshold, ThresholdDirection dir)
{
	_axis = MouseDef{ axis, rangeMax };
	set_threshold(threshold, dir);
}

// ----------------------------------------------------------------------------

void AxisBind2d::bind(int controllerIdx, ControllerAxis2d axis, glm::vec2 threshold, ThresholdDirection dir)
{
	_axis = ControllerDef{ axis, controllerIdx };
	set_threshold(threshold, dir);
}

// ----------------------------------------------------------------------------

glm::vec2 AxisBind2d::value() const
{
	assert(_axis.index() < 3);

	switch(_axis.index())
	{
	case 0:
		return { 0, 0 };
	case 1:
	{
		const auto &def = std::get<MouseDef>(_axis);

		const auto pos = Input::mousePosition();

		if(not _infinite.enabled)
		{
			return {
				float(pos.x) / float(def.max.x),
				float(pos.y) / float(def.max.y),
			};
		}

		Input::setMousePosition({ def.max.x/2, def.max.y/2 });
		_infinite.value += _infinite.scaler * glm::vec2(glm::ivec2(pos) - glm::ivec2(def.max)/2) / glm::vec2(def.max);

		auto apply_limits = [](float &value, const auto limits, auto limit_mode) {
			if(value <= limits.min)
			{
				if(limit_mode == LimitMode::Wrap)
					value += limits.max - limits.min;
				else
					value = limits.min;
			}
			if(value >= limits.max)
			{
				if(limit_mode == LimitMode::Wrap)
					value -= limits.max - limits.min;
				else
					value = limits.max;
			}
		};

		apply_limits(_infinite.value.x, _infinite.limits_x, _infinite.limit_mode_x);
		apply_limits(_infinite.value.y, _infinite.limits_y, _infinite.limit_mode_y);

		return _infinite.value;

	}
	case 2:
	{
		const auto &def = std::get<ControllerDef>(_axis);
		return Input::controllerAxis(def.idx, def.axis);
	}
	}

	std::unreachable();
}

// ----------------------------------------------------------------------------

glm::ivec2 AxisBind2d::rawValue() const
{
	assert(_axis.index() < 2);

	switch(_axis.index())
	{
	case 0:
		return { 0, 0 };
	case 1:
	{
		const auto pos = Input::mousePosition();
		return { pos.x, pos.y };
	}
	case 2:
	{
		const auto v = value();
		return {
			v.x*32767,
			v.y*32767,
		};
	}
	}

	std::unreachable();
}

// ----------------------------------------------------------------------------

bool AxisBind2d::triggered() const
{
	const auto v = value();
	if(_thresholdDirection == ThresholdDirection::Greater)
		return v.x >= _threshold.x or v.y >= _threshold.y;
	return v.x <= _threshold.x or v.y <= _threshold.y;
}

bool AxisBind2d::set_infinite(bool enabled)
{
	if(_axis.index() != 1) // only supported for mouse axis
		return false;

	if(_infinite.enabled == enabled)  // already in expected mode
		return true;

	if(not Input::setMouseInfiniteMode(enabled))
		return false; // unsupported or already in infinite mode

	_infinite.enabled = enabled;
	if(enabled)
	{
		const auto &def = std::get<MouseDef>(_axis);

		_infinite.value = { 0.f, 0.f };
		set_scaler({ 10.f, 10.f });
		set_no_limits_x();
		set_no_limits_y();
		Input::setMouseCursorVisible(false);
		const auto pos = Input::mousePosition();
		_infinite.grab_pos = pos;
		Input::setMousePosition({ def.max.x/2, def.max.x/2 });
	}
	else
	{
		Input::setMouseCursorVisible(true);
		Input::setMousePosition(_infinite.grab_pos);
	}

	return true;
}

// ----------------------------------------------------------------------------

void AxisBind2d::set_scaler(glm::vec2 scaler)
{
	assert(scaler.x != 0 and scaler.y != 0);
	// TODO: support negative scaler
	_infinite.scaler = scaler;
}

void AxisBind2d::set_limits_x(float min, float max, LimitMode mode)
{
	_infinite.limits_x = { min, max };
	_infinite.limit_mode_x = mode;
}

// ----------------------------------------------------------------------------

void AxisBind2d::set_limits_y(float min, float max, LimitMode mode)
{
	assert(min < max);
	if(max < min)
		std::swap(max, min);
	if(min != max)
	{
		_infinite.limits_y = { min, max };
		_infinite.limit_mode_y = mode;
	}
}

// ----------------------------------------------------------------------------

void AxisBind2d::set_no_limits_x()
{
	_infinite.limits_x = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN() };
}

// ----------------------------------------------------------------------------

void AxisBind2d::set_no_limits_y()
{
	_infinite.limits_y = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN() };
}

// ----------------------------------------------------------------------------

} // RGL
