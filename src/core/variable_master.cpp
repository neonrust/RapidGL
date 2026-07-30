#include "variable_master.h"
#include "variable.h"

#include <print>
#include <functional>

namespace RGL
{

struct closure
{
	inline closure(std::function<void()> &&f) : _f(f) {}
	inline ~closure()
	{
		if(_f)
			_f();
	}
private:
	std::function<void()> &_f;
};

VariableMaster *VariableMaster::the()
{
	static VariableMaster *instance = new VariableMaster();
	return instance;
}

bool VariableMaster::contains(std::string_view name)
{
	return _variables.contains(name);
}

VariableMaster::VariableMaster()
{
	_variables.reserve(64);
	_variableGroups.reserve(16);
}


VariableMaster::iterator VariableMaster::findVariable(std::string_view name)
{
	return _variables.find(name);
}


bool VariableMaster::load(const std::filesystem::path &filename, std::string_view pattern)
{
	std::print("VariableMaster::load() is NOT implemented. file: '{}'  pattern: '{}'", filename.native(), pattern);

	return false;
}


bool VariableMaster::save(const std::filesystem::path &filename, std::string_view pattern) const
{
	std::print("VariableMaster::save() is NOT implemented. file: '{}' pattern: '{}'", filename.native(), pattern);

	auto *out = std::fopen(filename.native().c_str(), "wb");
	closure _([out]{ std::fclose(out); });

	std::array<const char *, Variable::_StorageTypeCount> type_names {
		"i", "f", "s", "rgb", "rgba", "vec3", "vec4", "mat4",
	};
	static_assert(Variable::_StorageTypeCount == 8);

	for(const auto &[name, var]: _variables)
	{
		if((var->flags() & Variable::Archive) == 0)
			continue;
		// TODO: write only variables matching 'pattern'
		// if(zstr::match(pattern, name)!= 0)
		// 	continue;

		std::print(out, R"-("{}"\ttype:{})-", var->name(), type_names[var->type()]);

		// value is last & until EOL
		switch(var->type())
		{
		case Variable::INTEGER:
			std::println(out, "\tvalue:{}", var->integer());
			break;
		case Variable::REAL:
			std::println(out, "\tvalue:{}", var->real());
			break;
		case Variable::STRING:
			std::println(out, "\tvalue:{}", var->string());
			break;
		case Variable::RGBA:
		{
			const auto &c = var->rgba();
			std::println(out, "\tvalue:{} {} {} {}", c.r, c.g, c.b, c.a);
			break;
		}
		default:
			std::println("Serializing variable '{}' NOT implemented, type = {}", var->name(), uint32_t(var->type()));
			break;
		}
	}
#undef NAME_TYPE

	return false;
}

uint32_t VariableMaster::numVariables() const
{
	return uint32_t(_variables.size());
}

const VariableMaster::iterator VariableMaster::begin()
{
	return _variables.begin();
}

const VariableMaster::iterator VariableMaster::end()
{
	return _variables.end();
}

void VariableMaster::registerVariable(Variable *var)
{
	assert(var != nullptr);

	if(auto group = var->group(); not group.empty())
		_variableGroups[group][var->name()] = var;

	_variables[var->name()] = var;

// #ifdef _DEBUG
// 	switch(var->getType())
// 	{
// 		case Variable::INTEGER:
// 				Engine::logf(DEBUG_MSG, " RegVar: integer '{}' = {}  flags: {}",
// 						var->getName(),
// 						var->getInt(),
// 						var->getFlagsString());
// 				break;
// 		case Variable::FLOAT:
// 				Engine::logf(DEBUG_MSG, " RegVar: float   '{}' = {}  flags: {}",
// 						var->getName(),
// 						var->getFloat(),
// 						var->getFlagsString());
// 				break;
// 		case Variable::STRING:
// 				Engine::logf(DEBUG_MSG, " RegVar: string  '{}' = '{}'  flags: {}",
// 						var->getName(),
// 						var->toString(),
// 						var->getFlagsString());
// 				break;
// 		case Variable::COLOR:
// 				Engine::logf(DEBUG_MSG, " RegVar: color   '{}' = {}  flags: {}",
// 						var->getName(),
// 						var->toString(),
// 						var->getFlagsString());
// 				break;
// 	}
// #endif
}


void VariableMaster::deregisterVariable(Variable *var)
{
	auto found = _variables.find(var->name());
	assert(found != _variables.end());

	if(found != _variables.end())
		_variables.erase(found);

	if(auto group = var->group(); not group.empty())
		_variableGroups[group].erase(var->name());
}

void VariableMaster::variableChanged(Variable *var)
{
	auto found = _variables.find(var->name());
	assert(found != _variables.end());

	std::println("Variable '{}' changed value", var->name());
}

} // RGL