#include "input.h"
#include "log.h"

#include "input_bind.h"

#include <chrono>

namespace RGL
{

static GLFWwindow *s_window = nullptr;
static glm::uvec2 s_windowSize;

// state of all known keys (after update() is called)
static dense_map<KeyCode, bool> s_last_keys_states;
static dense_map<KeyCode, std::pair<bool, bool>> s_toggled_key_states;

// state of all known mouse buttons (after update() is called)
static dense_map<MouseButton, bool> s_last_mouse_states;
static dense_map<MouseButton, std::pair<bool, bool>> s_toggled_mouse_states;

static dense_set<int> s_active_controllers;
static dense_map<std::pair<int, ControllerButton>, bool> s_last_controller_states;
static dense_map<std::pair<int, ControllerButton>, std::pair<bool, bool>> s_toggled_controller_states;

static bool s_mouse_infinite_mode { false };

static dense_map<KeyCode, uint32_t>                          s_keys_used;
static dense_map<MouseButton, uint32_t>                      s_mouse_buttons_used;
static dense_map<std::pair<int, ControllerButton>, uint32_t> s_controller_buttons_used;


void Input::init(GLFWwindow *window)
{
	s_window = window;

	glfwGetWindowSize(s_window, reinterpret_cast<int *>(&s_windowSize.x), reinterpret_cast<int *>(&s_windowSize.y));

	s_keys_used.reserve(64);
	s_toggled_key_states.reserve(8u);

	s_mouse_buttons_used.reserve(6);
	s_toggled_mouse_states.reserve(6);

	s_active_controllers.reserve(2);
	s_controller_buttons_used.reserve(32);
	s_last_controller_states.reserve(32);
	s_toggled_controller_states.reserve(8);
}

using namespace std::chrono;

void Input::update()
{
	// const auto T0 = steady_clock::now();

	for(auto &[key, _]: s_keys_used)
	{
		s_last_keys_states[key] = isKeyDown(key);

		auto found = s_toggled_key_states.find(key);
		assert(found != s_toggled_key_states.end());
		auto &states = found->second;
		if(wasKeyPressed(key))
			states.first = not states.first;
		else if(wasKeyReleased(key))
			states.second = not states.second;
	}

	for(auto &[button, _]: s_mouse_buttons_used)
	{
		s_last_mouse_states[button] = isMouseDown(button);

		auto found = s_toggled_mouse_states.find(button);
		assert(found != s_toggled_mouse_states.end());
		auto &states = found->second;
		if(wasMousePressed(button))
			states.first = not states.first;
		else if(wasMouseReleased(button))
			states.second = not states.second;
	}

	for(auto [btn_key, _]: s_controller_buttons_used)
	{
		const auto &[idx, button] = btn_key;
		s_last_controller_states[btn_key] = isControllerButtonDown(idx, button);

		auto found = s_toggled_controller_states.find(btn_key);
		assert(found != s_toggled_controller_states.end());
		auto &states = found->second;
		if(wasControllerButtonPressed(idx, button))
			states.first = not states.first;
		else if(wasControllerButtonReleased(idx, button))
			states.second = not states.second;
	}

	// const auto T1 = steady_clock::now();
	// Log::debug("input state updated, in {}    (keys: {}, m-btns: {}, c-btns: {})",
	// 		   T1 - T0,
	// 		   s_keys_used.size(),
	// 		   s_mouse_buttons_used.size(),
	// 		   s_controller_buttons_used.size());
}

void Input::addUsage(KeyCode key)
{
	auto found = s_keys_used.find(key);
	if(found != s_keys_used.end())
		++found->second;
	else
	{
		s_keys_used[key] = 1;
		s_last_keys_states[key] = Input::isKeyDown(key);
		s_toggled_key_states[key] = { false, false };
	}
}

void Input::addUsage(MouseButton btn)
{
	auto found = s_mouse_buttons_used.find(btn);
	if(found != s_mouse_buttons_used.end())
		++found->second;
	else
	{
		s_mouse_buttons_used[btn] = 1;
		s_last_mouse_states[btn] = Input::isMouseDown(btn);
		s_toggled_mouse_states[btn] = { false, false };
	}
}

void Input::addUsage(std::pair<int, ControllerButton> btn)
{
	auto found = s_controller_buttons_used.find(btn);
	if(found != s_controller_buttons_used.end())
		++found->second;
	else
	{
		s_controller_buttons_used[btn] = 1;
		s_last_controller_states[btn] = Input::isControllerButtonDown(btn.first, btn.second);
		s_toggled_controller_states[btn] = { false, false };
	}
}

void Input::removeUsage(KeyCode key)
{
	auto found = s_keys_used.find(key);
	if(found != s_keys_used.end())
	{
		--found->second;
		if(--found->second == 0)
		{
			s_keys_used.erase(found);
			s_last_keys_states.erase(key);
			s_toggled_key_states.erase(key);
		}
	}
}

void Input::removeUsage(MouseButton btn)
{
	auto found = s_mouse_buttons_used.find(btn);
	if(found != s_mouse_buttons_used.end())
	{
		--found->second;
		if(--found->second == 0)
		{
			s_mouse_buttons_used.erase(found);
			s_last_mouse_states.erase(btn);
		}
	}
}

void Input::removeUsage(std::pair<int, ControllerButton> btn)
{
	auto found = s_controller_buttons_used.find(btn);
	if(found != s_controller_buttons_used.end())
	{
		--found->second;
		if(--found->second == 0)
		{
			s_controller_buttons_used.erase(found);
			s_last_controller_states.erase(btn);
		}
	}
}

void Input::activateController(int idx)
{
	const auto &[_, inserted] = s_active_controllers.insert(idx);
	if(inserted)
		Log::debug("Controller {} activated", idx);
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

bool Input::keyToggledState(KeyCode keyCode, bool toggleOnPress)
{
	auto &toggled = s_toggled_key_states[keyCode];
	return toggleOnPress? toggled.first: toggled.second;
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

bool Input::mouseToggledState(MouseButton button, bool toggleOnPress)
{
	auto &toggled = s_toggled_mouse_states[button];
	return toggleOnPress? toggled.first: toggled.second;
}

glm::uvec2 Input::mouseRange()
{
	return s_windowSize;
}

glm::vec2 Input::mousePosition()
{
	double x_pos, y_pos;
	glfwGetCursorPos(s_window, &x_pos, &y_pos);

	return glm::vec2(x_pos, y_pos);
}

void Input::setMousePosition(const glm::vec2 &pos)
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

bool Input::isMouseInfiniteMode()
{
	return s_mouse_infinite_mode;
}

bool Input::setMouseInfiniteMode(bool enable)
{
	if(enable and s_mouse_infinite_mode)
		return false;
	s_mouse_infinite_mode = enable;
	return true;
}

float Input::controllerAxis(int controllerIdx, ControllerAxis1d axis)
{
	assert(controllerIdx >= GLFW_JOYSTICK_1 and controllerIdx <= GLFW_JOYSTICK_LAST);

	GLFWgamepadstate state;
	if(glfwGetGamepadState(controllerIdx, &state))
		return state.axes[int(axis)];

	return 0;
}

glm::vec2 Input::controllerAxis(int controllerIdx, ControllerAxis2d axis)
{
	switch(axis)
	{
	case ControllerAxis2d::LeftThumb:
		return {
			controllerAxis(controllerIdx, ControllerAxis1d::LeftThumbH),
			controllerAxis(controllerIdx, ControllerAxis1d::LeftThumbV),
		};
	case ControllerAxis2d::RightThumb:
		return {
			controllerAxis(controllerIdx, ControllerAxis1d::RightThumbH),
			controllerAxis(controllerIdx, ControllerAxis1d::RightThumbV),
		};
	}
	std::unreachable();
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
	if(not s_last_controller_states.contains({ controllerIdx, button }))
	{
		Log::warning("Unreferenced controller button queried: {}/{}", controllerIdx, int(button));
		return false;
	}

	return s_last_controller_states[{ controllerIdx, button }];
}

bool Input::wasControllerButtonPressed(int controllerIdx, ControllerButton button)
{
	return not wasControllerButtonDown(controllerIdx, button) and isControllerButtonDown(controllerIdx, button);
}

bool Input::wasControllerButtonReleased(int controllerIdx, ControllerButton button)
{
	return wasControllerButtonDown(controllerIdx, button) and not isControllerButtonDown(controllerIdx, button);
}

bool Input::controllerButtonToggledState(int controllerIdx, ControllerButton button, bool toggleOnPress)
{
	auto &toggled = s_toggled_controller_states[{ controllerIdx, button }];
	return toggleOnPress? toggled.first: toggled.second;
}

} // RGL
