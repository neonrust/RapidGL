#include "variable_master.h"
#include "log.h"
#include "variable.h"

#include <functional>
#include <print>

namespace RGL
{

struct closure
{
	inline closure(std::function<void()> &&f) : _f(std::move(f)) {}
	inline ~closure()
	{
		if(_f)
			_f();
	}
private:
	std::function<void()> _f;
};

VariableMaster &VariableMaster::the()
{
	static VariableMaster *instance = new VariableMaster();
	return *instance;
}

bool VariableMaster::contains(std::string_view name)
{
	return _variables.contains(name);
}

VariableMaster::VariableMaster()
{
	_variables.reserve(64);
	_variable_groups.reserve(16);
}


VariableMaster::iterator VariableMaster::find(std::string_view name)
{
	return _variables.find(name);
}


bool VariableMaster::load(const std::filesystem::path &filename, std::string_view pattern)
{
	Log::warning("VariableMaster::load() is NOT implemented. file: '{}'  pattern: '{}'", filename.native(), pattern);

	return false;
}


bool VariableMaster::save(const std::filesystem::path &filename, std::string_view pattern) const
{
	auto *out = std::fopen(filename.native().c_str(), "wb");
	if(not out)
		return false;

	closure _([out]{ std::fclose(out); });

	std::array<const char *, Variable::_StorageType_count> type_names {
		"i", "f", "s", "v3", "v4", "m44",
	};
	static_assert(Variable::_StorageType_count == 6);

	for(const auto &[name, var]: _variables)
	{
		if((var->flags() & Variable::Archive) == 0)
			continue;
		// TODO: write only variables matching 'pattern'
		// if(zstr::match(pattern, name)!= 0)
		// 	continue;

		std::print(out, "{}\ttype:{}", var->name(), type_names[var->type()]);

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
			assert(not var->string().contains("\n"));
			std::println(out, "\tvalue:{}", var->string());
			break;
		case Variable::VEC3:
			std::println(out, "\tvalue:{};{};{}", var->vec3().x, var->vec3().y, var->vec3().z);
			break;
		case Variable::VEC4:
			std::println(out, "\tvalue:{};{};{};{}", var->vec4().x, var->vec4().y, var->vec4().z, var->vec4().w);
			break;
		case Variable::MAT4:
		{
			const auto &m = var->mat4();
			std::println(out, "\tvalue:{};{};{};{};{};{};{};{};{};{};{};{};{};{};{};{}",
						 m[0].x, m[0].y, m[0].z, m[0].w,
						 m[1].x, m[1].y, m[1].z, m[1].w,
						 m[2].x, m[2].y, m[2].z, m[2].w,
						 m[3].x, m[3].y, m[3].z, m[3].w);
			break;
		}
		default:
			std::println(out, "NOT implemented");
			break;
		}
	}
#undef NAME_TYPE

	return true;
}

uint32_t VariableMaster::num_variables() const
{
	return uint32_t(_variables.size());
}

SubscriberID VariableMaster::subscribe(std::string_view name_prefix, VariableChangedCB callback) // pre(name_prefix.size()  < 16)
{
	assert(name_prefix.size() < 16);

	auto id = _next_subscriber_id++;

	_subscribers[id] = { std::string(name_prefix), callback };

	// add subscriber to matching variables
	for(const auto &[name, var]: _variables)
	{
		if(var->name() >= name_prefix and var->name().starts_with(name_prefix))
			var->_subscribers.emplace(id);
	}

	return id;
}

bool VariableMaster::unsubscribe(SubscriberID id)
{
	auto found = _subscribers.find(id);
	assert(found != _subscribers.end());
	if(found == _subscribers.end())
		return false;

	// remove from all variables (faster than performing prefix match?)
	for(const auto &[name, var]: _variables)
	{
		// if(var->name() >= found->second.name_prefix and var->name().starts_with(found->second.name_prefix))
		var->_subscribers.erase(id);
	}

	_subscribers.erase(id);

	return true;
}

const VariableMaster::iterator VariableMaster::begin()
{
	return _variables.begin();
}

const VariableMaster::iterator VariableMaster::end()
{
	return _variables.end();
}

void VariableMaster::register_variable(Variable *var)
{
	assert(var != nullptr);

	if(auto group = var->group(); not group.empty())
		_variable_groups[group][var->name()] = var;

	_variables[var->name()] = var;

	// add matching subscribers to variable
	for(const auto &[id, subscriber]: _subscribers)
	{
		if(var->name().starts_with(subscriber.name_prefix))
			var->_subscribers.insert(id);
	}
}

void VariableMaster::deregister_variable(Variable *var)
{
	auto found = _variables.find(var->name());
	assert(found != _variables.end());

	if(found != _variables.end())
		_variables.erase(found);

	if(auto group = var->group(); not group.empty())
		_variable_groups[group].erase(var->name());
}

void VariableMaster::notify_subscribers(Variable *var)
{
	// make a copy, in case a callback causes a subscribe/unsubscribe
	auto subscribers = var->_subscribers;
	for(auto id: subscribers)
	{
		auto found = _subscribers.find(id);
		if(found != _subscribers.end())
		{
			auto callback = found->second.callback; // copy to avoid use-after-free
			callback(var);
		}
	}
}

} // RGL