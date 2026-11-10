#include "control_map.h"
#include "log.h"

namespace RGL
{

ControlMap::ControlMap() :
	_name("unnamed")
{
}

// ----------------------------------------------------------------------------

void ControlMap::set_name(std::string_view name)
{
	_name = std::string(name);
}

// ----------------------------------------------------------------------------

bool ControlMap::add(std::string_view name, ActionBind *bind)
{
	const auto &[_, inserted] = _actions.emplace(std::string(name), bind);
	return inserted;
}

// ----------------------------------------------------------------------------

bool ControlMap::add(std::string_view name, AxisBind *bind)
{
	const auto &[_, inserted] = _axes.emplace(std::string(name), bind);
	return inserted;
}

// ----------------------------------------------------------------------------

ActionBind &ControlMap::action(std::string_view bind_name)
{
	auto found = _actions.find(bind_name);
	if(found == _actions.end())
	{
		Log::warning("No action bind '{}' in control map '{}'", bind_name, _name);
		assert(found != _actions.end());
		static ActionBind sentinel;
		return sentinel;
	}
	return *found->second;
}

// ----------------------------------------------------------------------------

const ActionBind &ControlMap::action(std::string_view bind_name) const
{
	return const_cast<ControlMap *>(this)->action(bind_name);
}

// ----------------------------------------------------------------------------

AxisBind1d &ControlMap::axis(std::string_view bind_name)
{
	auto found = _axes.find(bind_name);
	if(found == _axes.end() or found->second->num_dimensions() != 1)
	{
		Log::warning("No axis (1d) bind '{}' in control map '{}'", bind_name, _name);
		assert(found != _axes.end());
		static AxisBind1d sentinel;
		return sentinel;
	}
	return found->second->as_1d();
}

// ----------------------------------------------------------------------------

AxisBind2d &ControlMap::axis2d(std::string_view bind_name)
{
	auto found = _axes.find(bind_name);
	if(found == _axes.end() or found->second->num_dimensions() != 2)
	{
		Log::warning("No axis (2d) bind '{}' in control map '{}'", bind_name, _name);
		assert(found != _axes.end());
		static AxisBind2d sentinel;
		return sentinel;
	}
	return found->second->as_2d();
}

// ----------------------------------------------------------------------------

const AxisBind2d &ControlMap::axis2d(std::string_view bind_name) const
{
	return const_cast<ControlMap *>(this)->axis2d(bind_name);
}

// ----------------------------------------------------------------------------

bool ControlMap::verify_bound()
{
	bool ok = true;

	for(const auto &[name, axis]: _axes)
	{
		if(not axis->is_bound())
		{
			Log::warning("Axis '{}' is not bound", name);
			ok = false;
		}
	}

	for(const auto &[name, action]: _actions)
	{
		if(not action->is_bound())
		{
			Log::warning("Action '{}' is not bound", name);
			ok = false;
		}
	}

	return ok;
}

} // RGL