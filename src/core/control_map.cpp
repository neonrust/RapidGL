#include "control_map.h"
#include "log.h"

namespace RGL
{

ControlMap::ControlMap(std::string_view name) :
	_name(name)
{
}

bool ControlMap::add(std::string_view name, ActionBind *bind)
{
	const auto &[_, inserted] = _actions.emplace(std::string(name), bind);
	return inserted;
}

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

const ActionBind &ControlMap::action(std::string_view bind_name) const
{
	return const_cast<ControlMap *>(this)->action(bind_name);
}

} // RGL