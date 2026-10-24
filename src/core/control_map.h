#pragma once

#include "input_bind.h"
#include "container_types.h"


namespace RGL
{

class ControlMap
{
public:
	ControlMap(std::string_view name);

	bool add(std::string_view name, ActionBind *bind);
	// bool add(std::string_view name, const AxisBind   *bind);

	inline const string_map<ActionBind *> &actions()       { return _actions; };
	inline const string_map<ActionBind *> &actions() const { return _actions; };
	// const string_map<AxisBind *> &axes();

		  ActionBind &action(std::string_view name);
	const ActionBind &action(std::string_view name) const;

private:
	string_map<ActionBind *> _actions;
	// string_map<AxisBind *> _axes;

	std::string _name;
};

} // RGL