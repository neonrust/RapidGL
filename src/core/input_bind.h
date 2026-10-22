#pragma once

#include "input.h"

// TODO: struct SteerBind  (mouse pos, joystick/gamepad axes)

namespace RGL
{

enum MouseButton
{
	MouseLeft = GLFW_MOUSE_BUTTON_LEFT,
	MouseMiddle = GLFW_MOUSE_BUTTON_MIDDLE,
	MouseRight = GLFW_MOUSE_BUTTON_RIGHT,
	Mouse1 = GLFW_MOUSE_BUTTON_1,
	Mouse2 = GLFW_MOUSE_BUTTON_2,
	Mouse3 = GLFW_MOUSE_BUTTON_3,
	Mouse4 = GLFW_MOUSE_BUTTON_4,
	Mouse5 = GLFW_MOUSE_BUTTON_5,
	Mouse6 = GLFW_MOUSE_BUTTON_6,
	Mouse7 = GLFW_MOUSE_BUTTON_7,
	Mouse8 = GLFW_MOUSE_BUTTON_8
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

private:
	bool check_state() const;

	enum { Key, MouseBtn, ControllerBtn } _type { Key };
	Trigger _trigger;
	union {
		KeyCode _key { KeyCode::None };
		MouseButton _mouseBtn;
		ControllerButton _controllerBtn;
	};
	int _controllerIdx { -1 }; // only used for _type == ControllerBtn
	mutable bool _previousState { false };

public:
	inline operator bool () const { return not (_type == Key and _key == KeyCode::None); }
};

// ----------------------------------------------------------------------------

inline ActionBind::ActionBind()
{
}

// ----------------------------------------------------------------------------

inline ActionBind::ActionBind(KeyCode key, Trigger trigger) :
	_type(Key),
	_trigger(trigger),
	_key(key)
{
}

// ----------------------------------------------------------------------------

inline ActionBind::ActionBind(MouseButton button, Trigger trigger) :
	_type(MouseBtn),
	_trigger(trigger),
	_mouseBtn(button)
{
}

// ----------------------------------------------------------------------------

inline ActionBind::ActionBind(int controllerIdx, ControllerButton button, Trigger trigger) :
	_type(ControllerBtn),
	_trigger(trigger),
	_controllerBtn(button),
	_controllerIdx(controllerIdx)
{
}

// ----------------------------------------------------------------------------

inline bool ActionBind::check_state() const
{
	switch(_type)
	{
	case Key:
		return Input::wasKeyPressed(_key);
	case MouseBtn:
		return Input::wasMousePressed(KeyCode(_mouseBtn));
	case ControllerBtn:
		return Input::getControllerButton(_controllerIdx, int(_controllerBtn));
	}

	return false;
}

} // RGL
