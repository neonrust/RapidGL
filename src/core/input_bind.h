#pragma once

#include "input.h"

namespace RGL
{

enum class ControllerAxis
{
	LeftThumb,
	RightThumb,

	LeftBumper,
	RightBumper,
};

struct ActionBind
{
	enum Trigger {
		Press,   // once
		Hold,    // continuously, until released
		Repeat,  // like hold, but fires at the defined rate
		Release  // oppositve of Press; once, but when released
	};
	enum TriggerState
	{
		NotTriggered,
		Triggered,
		TriggerStill,   // only trigger == Hold
		TriggerEnd      // only trigger == Hold
	};

	ActionBind();
	ActionBind(KeyCode key, Trigger trigger=Press);
	ActionBind(MouseButton button, Trigger trigger=Press);
	ActionBind(int controllerIdx, ControllerButton button, Trigger trigger=Press);

	std::string description() const;

	KeyCode key() const;
	MouseButton button() const;
	std::pair<size_t, ControllerButton> controllerButton() const;

	TriggerState triggered() const;
	operator bool () const;

private:
	bool current_state() const;
	bool previous_state() const;

	Trigger _trigger;
	struct ControllerDef
	{
		ControllerButton button;
		int idx { -1 }; // only used for _type == ControllerBtn
	};
	std::variant<KeyCode, MouseButton, ControllerDef> _button { KeyCode::None };
};

// ----------------------------------------------------------------------------

enum class ThresholdDirection { Greater, Less };

struct AxisBind
{
	AxisBind();
	AxisBind(MouseAxis axis, uint_fast16_t rangeMax, float threshold=std::numeric_limits<float>::quiet_NaN(), ThresholdDirection dir=ThresholdDirection::Greater);
	AxisBind(int controllerIdx, ControllerAxis axis, float threshold=std::numeric_limits<float>::quiet_NaN(), ThresholdDirection dir=ThresholdDirection::Greater);

	float value() const;
	int_fast16_t rawValue() const;

	bool triggered() const;
	operator bool () const;

	bool hitThreshold() const;

	void setThreshold(float threshold) { setThreshold(threshold, _thresholdDirection); };
	void setThreshold(float threshold, ThresholdDirection dir) { _threshold = threshold; _thresholdDirection = dir; }
	std::tuple<float, ThresholdDirection> threshold() const { return { _threshold, _thresholdDirection }; };

private:
	struct MouseDef
	{
		MouseAxis axis;
		float max;
	};
	struct ControllerDef
	{
		ControllerAxis axis;
		int idx { -1 }; // only used for _type == ControllerBtn
	};
	std::variant<MouseDef, ControllerDef> _axis;
	float _threshold { std::numeric_limits<float>::quiet_NaN() };
	ThresholdDirection _thresholdDirection { ThresholdDirection::Greater};
};

} // RGL
