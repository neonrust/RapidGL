#pragma once

#include "input.h"
#include <glm/vec4.hpp>

namespace RGL
{

class ActionBind
{
public:
	enum Trigger
	{
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

public:
	ActionBind();
	ActionBind(KeyCode key, Trigger trigger=Press);
	ActionBind(MouseButton button, Trigger trigger=Press);
	ActionBind(int controllerIdx, ControllerButton button, Trigger trigger=Press);

	void bind(KeyCode key, Trigger trigger=Press);
	void bind(MouseButton button, Trigger trigger=Press);
	void bind(int controllerIdx, ControllerButton button, Trigger trigger=Press);

	[[nodiscard]] bool is_bound() const { return _button.index() > 0; }

	[[nodiscard]] std::string description() const;

	[[nodiscard]] KeyCode key() const;
	[[nodiscard]] MouseButton button() const;
	[[nodiscard]] std::pair<size_t, ControllerButton> controllerButton() const;

	[[nodiscard]] TriggerState triggered() const;
	[[nodiscard]] inline operator bool () const { return triggered() != TriggerState::NotTriggered; };

private:
	[[nodiscard]] bool current_state() const;
	[[nodiscard]] bool previous_state() const;

	Trigger _trigger;
	struct ControllerDef
	{
		ControllerButton button;
		int idx { -1 }; // only used for _type == ControllerBtn
	};
	std::variant<std::monostate, KeyCode, MouseButton, ControllerDef> _button { KeyCode::None };
};

// ----------------------------------------------------------------------------

enum class ThresholdDirection { Greater, Less };

class AxisBind1d;
class AxisBind2d;

class AxisBind
{
public:
	[[nodiscard]] virtual std::string description() const = 0;

	[[nodiscard]] virtual bool is_bound() const = 0;

	[[nodiscard]] virtual size_t num_dimensions() const = 0;
	[[nodiscard]] bool is1d() const { return num_dimensions() == 1; }
	[[nodiscard]] bool is2d() const { return num_dimensions() == 1; }

	[[nodiscard]] AxisBind1d &as_1d();
	[[nodiscard]] AxisBind2d &as_2d();

	[[nodiscard]] virtual bool triggered() const = 0;

	[[nodiscard]] operator bool () const { return triggered(); }
	[[nodiscard]] bool hit_threshold() const { return triggered(); }

	enum class LimitMode { Clamp, Wrap };
};

class AxisBind1d : public AxisBind
{
public:
	AxisBind1d();
	AxisBind1d(MouseAxis1d axis, uint_fast16_t rangeMax, float threshold=std::numeric_limits<float>::quiet_NaN(), ThresholdDirection dir=ThresholdDirection::Greater);
	AxisBind1d(int controllerIdx, ControllerAxis1d axis, float threshold=std::numeric_limits<float>::quiet_NaN(), ThresholdDirection dir=ThresholdDirection::Greater);

	void bind(MouseAxis1d axis, uint_fast16_t rangeMax, float threshold=std::numeric_limits<float>::quiet_NaN(), ThresholdDirection dir=ThresholdDirection::Greater);
	void bind(int controllerIdx, ControllerAxis1d axis, float threshold=std::numeric_limits<float>::quiet_NaN(), ThresholdDirection dir=ThresholdDirection::Greater);

	[[nodiscard]] std::string description() const override;

	[[nodiscard]] bool is_bound() const override { return _axis.index() > 0; }

	[[nodiscard]] virtual size_t num_dimensions() const override { return 1; }

	[[nodiscard]] float value() const;
	[[nodiscard]] int_fast16_t rawValue() const;

	[[nodiscard]] bool triggered() const override;

	void set_threshold(float threshold) { set_threshold(threshold, _thresholdDirection); };
	void set_threshold(float threshold, ThresholdDirection dir) { _threshold = threshold; _thresholdDirection = dir; }
	[[nodiscard]] std::tuple<float, ThresholdDirection> threshold() const { return { _threshold, _thresholdDirection }; };

	bool set_infinite(bool enabled); // TODO: better name than 'infinite'.  'flat'?
	void set_scaler(float scaler);
	bool set_infinite(bool enabled, float min, float max, LimitMode mode=LimitMode::Clamp);
	void set_limits(float min, float max, LimitMode behaviour);
	void set_no_limits();

private:
	struct MouseDef
	{
		MouseAxis1d axis;
		float max;
	};
	struct ControllerDef
	{
		ControllerAxis1d axis;
		int idx { -1 };
	};
	std::variant<std::monostate, MouseDef, ControllerDef> _axis;
	float _threshold;
	ThresholdDirection _thresholdDirection { ThresholdDirection::Greater};

	struct {
		bool  enabled { false};
		mutable float value { 0.f };
		glm::uvec2 grab_pos { 0, 0 };
		float scaler        { 1.f };
		LimitMode           limit_mode;
		struct { float min; float max; } limits;
	} _infinite;
};

class AxisBind2d : public AxisBind
{
public:
	AxisBind2d();
	AxisBind2d(MouseAxis2d axis, glm::uvec2 rangeMax, glm::vec2 threshold={ std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN() }, ThresholdDirection dir=ThresholdDirection::Greater);
	AxisBind2d(int controllerIdx, ControllerAxis2d axis, glm::vec2 threshold={ std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN() }, ThresholdDirection dir=ThresholdDirection::Greater);

	void bind(MouseAxis2d axis, glm::uvec2 rangeMax, glm::vec2 threshold={ std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN() }, ThresholdDirection dir=ThresholdDirection::Greater);
	void bind(int controllerIdx, ControllerAxis2d axis, glm::vec2 threshold={ std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN() }, ThresholdDirection dir=ThresholdDirection::Greater);

	[[nodiscard]] std::string description() const override;

	[[nodiscard]] bool is_bound() const override { return _axis.index() > 0; }

	[[nodiscard]] virtual size_t num_dimensions() const override { return 2; }

	[[nodiscard]] glm::vec2 value() const;
	[[nodiscard]] glm::ivec2 rawValue() const;

	[[nodiscard]] bool triggered() const override;

	void set_threshold(glm::vec2 threshold) { set_threshold(threshold, _thresholdDirection); };
	void set_threshold(glm::vec2 threshold, ThresholdDirection dir) { _threshold = threshold; _thresholdDirection = dir; };
	[[nodiscard]] std::tuple<glm::vec2, ThresholdDirection> threshold() const { return { _threshold, _thresholdDirection }; };

	bool set_infinite(bool enabled);
	void set_scaler(glm::vec2 scaler);
	void set_limits_x(float min, float max, LimitMode mode);
	void set_limits_y(float min, float max, LimitMode mode);
	void set_no_limits_x();
	void set_no_limits_y();

private:
	struct MouseDef
	{
		MouseAxis2d axis;
		glm::uvec2 max;
	};
	struct ControllerDef
	{
		ControllerAxis2d axis;
		int idx { -1 };
	};
	std::variant<std::monostate, MouseDef, ControllerDef> _axis;
	glm::vec2 _threshold { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN() };
	ThresholdDirection _thresholdDirection { ThresholdDirection::Greater};

	struct {
		bool enabled { false };
		mutable glm::vec2 value;
		glm::uvec2    grab_pos;
		glm::vec2     scaler;
		LimitMode     limit_mode_x;
		struct { float min; float max; } limits_x;
		LimitMode     limit_mode_y;
		struct { float min; float max; } limits_y;
	} _infinite;
};

} // RGL
