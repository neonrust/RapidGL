#pragma once

#include "input.h"

namespace RGL
{

enum MouseButton
{
	Left = GLFW_MOUSE_BUTTON_LEFT,
	Middle = GLFW_MOUSE_BUTTON_MIDDLE,
	Right = GLFW_MOUSE_BUTTON_RIGHT,
	Mouse1 = GLFW_MOUSE_BUTTON_1,  // left
	Mouse2 = GLFW_MOUSE_BUTTON_2,  // right
	Mouse3 = GLFW_MOUSE_BUTTON_3,  // middle
	Mouse4 = GLFW_MOUSE_BUTTON_4,
	Mouse5 = GLFW_MOUSE_BUTTON_5,
	Mouse6 = GLFW_MOUSE_BUTTON_6,
	Mouse7 = GLFW_MOUSE_BUTTON_7,
	Mouse8 = GLFW_MOUSE_BUTTON_8
};

enum MouseAxis
{
	Horizontal,
	Vertical,
};

enum class ControllerButton
{
	A = GLFW_GAMEPAD_BUTTON_A,
	B = GLFW_GAMEPAD_BUTTON_B,
	X = GLFW_GAMEPAD_BUTTON_X,
	Y = GLFW_GAMEPAD_BUTTON_Y,

	CROSS = GLFW_GAMEPAD_BUTTON_CROSS,
	CIRCLE = GLFW_GAMEPAD_BUTTON_CIRCLE,
	SQUARE = GLFW_GAMEPAD_BUTTON_SQUARE,
	TRIANGLE = GLFW_GAMEPAD_BUTTON_TRIANGLE,

	LB = GLFW_GAMEPAD_BUTTON_LEFT_BUMPER,
	RB = GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER,

	LT = GLFW_GAMEPAD_BUTTON_LEFT_THUMB,
	RT = GLFW_GAMEPAD_BUTTON_RIGHT_THUMB,

	UP = GLFW_GAMEPAD_BUTTON_DPAD_UP,
	RIGHT = GLFW_GAMEPAD_BUTTON_DPAD_RIGHT,
	DOWN = GLFW_GAMEPAD_BUTTON_DPAD_DOWN,
	LEFT = GLFW_GAMEPAD_BUTTON_DPAD_LEFT,

	START = GLFW_GAMEPAD_BUTTON_START,
	BACK = GLFW_GAMEPAD_BUTTON_BACK,
	GUIDE = GLFW_GAMEPAD_BUTTON_GUIDE,
};

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
