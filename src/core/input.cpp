#include "input.h"
#include "log.h"

namespace RGL
{

static GLFWwindow *s_window = nullptr;

// state of all known keys (after update() is called)
static dense_map<KeyCode, bool> s_last_keys_states = {
	{ KeyCode::Backspace,      false },
	{ KeyCode::Delete,         false },
	{ KeyCode::Tab,            false },
	{ KeyCode::Return,         false },
	{ KeyCode::Pause,          false },
	{ KeyCode::Escape,         false },
	{ KeyCode::Space,          false },
	{ KeyCode::Keypad0,        false },
	{ KeyCode::Keypad1,        false },
	{ KeyCode::Keypad2,        false },
	{ KeyCode::Keypad3,        false },
	{ KeyCode::Keypad4,        false },
	{ KeyCode::Keypad5,        false },
	{ KeyCode::Keypad6,        false },
	{ KeyCode::Keypad7,        false },
	{ KeyCode::Keypad8,        false },
	{ KeyCode::Keypad9,        false },
	{ KeyCode::KeypadPeriod,   false },
	{ KeyCode::KeypadDivide,   false },
	{ KeyCode::KeypadMultiply, false },
	{ KeyCode::KeypadMinus,    false },
	{ KeyCode::KeypadPlus,     false },
	{ KeyCode::KeypadEnter,    false },
	{ KeyCode::KeypadEquals,   false },
	{ KeyCode::UpArrow,        false },
	{ KeyCode::DownArrow,      false },
	{ KeyCode::RightArrow,     false },
	{ KeyCode::LeftArrow,      false },
	{ KeyCode::Insert,         false },
	{ KeyCode::Home,           false },
	{ KeyCode::End,            false },
	{ KeyCode::PageUp,         false },
	{ KeyCode::PageDown,       false },
	{ KeyCode::F1,             false },
	{ KeyCode::F2,             false },
	{ KeyCode::F3,             false },
	{ KeyCode::F4,             false },
	{ KeyCode::F5,             false },
	{ KeyCode::F6,             false },
	{ KeyCode::F7,             false },
	{ KeyCode::F8,             false },
	{ KeyCode::F9,             false },
	{ KeyCode::F10,            false },
	{ KeyCode::F11,            false },
	{ KeyCode::F12,            false },
	{ KeyCode::F13,            false },
	{ KeyCode::F14,            false },
	{ KeyCode::F15,            false },
	{ KeyCode::Alpha0,         false },
	{ KeyCode::Alpha1,         false },
	{ KeyCode::Alpha2,         false },
	{ KeyCode::Alpha3,         false },
	{ KeyCode::Alpha4,         false },
	{ KeyCode::Alpha5,         false },
	{ KeyCode::Alpha6,         false },
	{ KeyCode::Alpha7,         false },
	{ KeyCode::Alpha8,         false },
	{ KeyCode::Alpha9,         false },
	{ KeyCode::Comma,          false },
	{ KeyCode::Minus,          false },
	{ KeyCode::Period,         false },
	{ KeyCode::Slash,          false },
	{ KeyCode::Semicolon,      false },
	{ KeyCode::Equals,         false },
	{ KeyCode::LeftBracket,    false },
	{ KeyCode::RightBracket,   false },
	{ KeyCode::Backslash,      false },
	{ KeyCode::A,              false },
	{ KeyCode::B,              false },
	{ KeyCode::C,              false },
	{ KeyCode::D,              false },
	{ KeyCode::E,              false },
	{ KeyCode::F,              false },
	{ KeyCode::G,              false },
	{ KeyCode::H,              false },
	{ KeyCode::I,              false },
	{ KeyCode::J,              false },
	{ KeyCode::K,              false },
	{ KeyCode::L,              false },
	{ KeyCode::M,              false },
	{ KeyCode::N,              false },
	{ KeyCode::O,              false },
	{ KeyCode::P,              false },
	{ KeyCode::Q,              false },
	{ KeyCode::R,              false },
	{ KeyCode::S,              false },
	{ KeyCode::T,              false },
	{ KeyCode::U,              false },
	{ KeyCode::V,              false },
	{ KeyCode::W,              false },
	{ KeyCode::X,              false },
	{ KeyCode::Y,              false },
	{ KeyCode::Z,              false },
	{ KeyCode::Numlock,        false },
	{ KeyCode::CapsLock,       false },
	{ KeyCode::ScrollLock,     false },
	{ KeyCode::RightShift,     false },
	{ KeyCode::LeftShift,      false },
	{ KeyCode::RightControl,   false },
	{ KeyCode::LeftControl,    false },
	{ KeyCode::RightAlt,       false },
	{ KeyCode::LeftAlt,        false },
	{ KeyCode::PrintScreen,    false },
	{ KeyCode::Menu,           false },
	};

// state of all known mouse buttons (after update() is called)
static dense_map<MouseButton, bool> s_last_mouse_states = {
	{ MouseButton::Left,       false },
	{ MouseButton::Middle,     false },
	{ MouseButton::Right,      false },
	{ MouseButton::Mouse4,     false },
	{ MouseButton::Mouse5,     false },
	{ MouseButton::Mouse6,     false },
	};

static dense_set<int> s_active_controllers;
static dense_map<int, dense_map<ControllerButton, bool>> s_last_controller_states;

static const dense_set<ControllerButton> s_controller_buttons = {
	ControllerButton::A,
	ControllerButton::B,
	ControllerButton::X,
	ControllerButton::Y,
	ControllerButton::LeftBumper,
	ControllerButton::RightBumper,
	ControllerButton::Back,
	ControllerButton::Start,
	ControllerButton::Guide,
	ControllerButton::LeftThumb,
	ControllerButton::RightThumb,
	ControllerButton::Up,
	ControllerButton::Right,
	ControllerButton::Down,
	ControllerButton::Left

};

void Input::init(GLFWwindow* window)
{
	s_window = window;
	s_last_controller_states.reserve(4);
}

void Input::update()
{
	for(auto &[key, state]: s_last_keys_states)
		state = isKeyDown(key);

	for(auto &[button, state]: s_last_mouse_states)
		state = isMouseDown(button);

	for(auto idx: s_active_controllers)
	{
		for(auto &[button, state]: s_last_controller_states[idx])
			state = isControllerButtonDown(idx, button);
	}
}

void Input::activateController(int idx)
{
	const auto &[_, inserted] = s_active_controllers.insert(idx);
	if(inserted)
		populateControllerButtons(idx);
}

void Input::populateControllerButtons(int idx)
{
	auto &m = s_last_controller_states[idx];

	for(auto button: s_controller_buttons)
		m[button] = isControllerButtonDown(idx, button);
}

bool Input::isKeyDown(KeyCode keyCode)
{
	return glfwGetKey(s_window, static_cast<int>(keyCode)) == GLFW_PRESS;
}

bool Input::wasKeyDown(KeyCode keyCode)
{
	return s_last_keys_states[keyCode];
}

bool Input::wasKeyPressed(KeyCode keyCode)
{
	return not wasKeyDown(keyCode) and isKeyDown(keyCode);
}

bool Input::wasKeyReleased(KeyCode keyCode)
{
	return wasKeyDown(keyCode) and not isKeyDown(keyCode);
}

bool Input::isMouseDown(MouseButton button)
{
	return glfwGetMouseButton(s_window, static_cast<int>(button)) == GLFW_PRESS;
}

bool Input::wasMouseDown(MouseButton button)
{
	return s_last_mouse_states[button];
}

bool Input::wasMousePressed(MouseButton button)
{
	return not wasMouseDown(button) and isMouseDown(button);
}

bool Input::wasMouseReleased(MouseButton button)
{
	return s_last_mouse_states[button] and not isMouseDown(button);
}

glm::uvec2 Input::mousePosition()
{
	double x_pos, y_pos;
	glfwGetCursorPos(s_window, &x_pos, &y_pos);

	return glm::uvec2(x_pos, y_pos);
}

void Input::setMousePosition(const glm::uvec2 & pos)
{
	glfwSetCursorPos(s_window, pos.x, pos.y);
}

bool Input::enableRawMouseMotion(bool enabled)
{
	if (glfwRawMouseMotionSupported())
	{
		glfwSetInputMode(s_window, GLFW_RAW_MOUSE_MOTION, enabled? GLFW_TRUE: GLFW_FALSE);
		return true;
	}
	return false;
}

bool Input::isRawMouseMotionEnabled()
{
	return glfwRawMouseMotionSupported() and
		glfwGetInputMode(s_window, GLFW_RAW_MOUSE_MOTION) == GLFW_TRUE;
}

void Input::setMouseCursorVisible(bool visible)
{
	glfwSetInputMode(s_window, GLFW_CURSOR, visible? GLFW_CURSOR_NORMAL: GLFW_CURSOR_DISABLED);
}

float Input::controllerAxis(int controllerIdx, int axis)
{
	assert(controllerIdx >= GLFW_JOYSTICK_1 and controllerIdx <= GLFW_JOYSTICK_LAST);

	GLFWgamepadstate state;
	if(glfwGetGamepadState(controllerIdx, &state))
		return state.axes[axis];

	return 0;
}

bool Input::isControllerButtonDown(int controllerIdx, ControllerButton button)
{
	assert(controllerIdx >= GLFW_JOYSTICK_1 and controllerIdx <= GLFW_JOYSTICK_LAST);

	GLFWgamepadstate state;
	if(glfwGetGamepadState(controllerIdx, &state))
		return state.buttons[int(button)] == GLFW_PRESS;

	return false;
}

bool Input::wasControllerButtonDown(int controllerIdx, ControllerButton button)
{
	assert(s_last_controller_states.contains(controllerIdx));

	const auto &buttons = s_last_controller_states[controllerIdx];

	auto bfound = buttons.find(button);
	if(bfound == buttons.end())
	{
		Log::warning("controller {} used unexpected button: {}", controllerIdx, int(button));
		assert(bfound != buttons.end());
		return false;
	}
	return bfound->second;
}

bool Input::wasControllerButtonPressed(int controllerIdx, ControllerButton button)
{
	return not wasControllerButtonDown(controllerIdx, button) and isControllerButtonDown(controllerIdx, button);
}

bool Input::wasControllerButtonReleased(int controllerIdx, ControllerButton button)
{
	return wasControllerButtonDown(controllerIdx, button) and not isControllerButtonDown(controllerIdx, button);
}

} // RGL
