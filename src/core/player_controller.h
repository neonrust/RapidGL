#pragma once

#include "control_map.h"
#include "movement_controller.h"


namespace RGL
{

class PlayerController : public MovementController
{
public:
	inline PlayerController(entt::entity subject_id) : MovementController(subject_id) {}

	inline       ControlMap &controls()       { return _controls; };
	inline const ControlMap &controls() const { return _controls; };

private:
	ControlMap _controls;
};

} // RGL