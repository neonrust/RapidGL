#include "variable.h"
#include "log.h"
#include "variable_master.h"

#include <format>


namespace RGL
{

// create Integer variable
Variable::Variable(std::string_view name, int_t value, BitMask32 flags) :
	_type(INTEGER),
	_flags(flags)
{
	set_name(name);

	_current = value;
	_default = value;

	if((_flags & User) == 0 and (_flags & ReadOnly) == 0)
		_flags |= Archive;
	
	VariableMaster::the().register_variable(this);
}

// create Float variable
Variable::Variable(std::string_view name, real_t value, BitMask32 flags) :
	_type(REAL),
	_flags(flags)
{
	set_name(name);

	_current = value;
	_default = value;

	if((_flags & User) == 0 and (_flags & ReadOnly) == 0)
		_flags |= Archive;

	VariableMaster::the().register_variable(this);
}

// create String variable
Variable::Variable(std::string_view name, std::string_view value, BitMask32 flags) :
	_type(STRING),
	_flags(flags)
{
	set_name(name);

	_current = std::string(value);
	_default = std::string(value);
	
	if((_flags & User) == 0 and (_flags & ReadOnly) == 0)
		_flags |= Archive;

	VariableMaster::the().register_variable(this);
}

Variable::Variable(std::string_view name, const glm::vec3 &value, BitMask32 flags) :
	_type(VEC3),
	_flags(flags)
{
	set_name(name);

	_current = value;
	_default = value;

	if((_flags & User) == 0 and (_flags & ReadOnly) == 0)
		_flags |= Archive;

	VariableMaster::the().register_variable(this);
}

Variable::Variable(std::string_view name, const glm::vec4 &value, BitMask32 flags) :
	_type(VEC4),
	_flags(flags)
{
	set_name(name);

	_current = value;
	_default = value;

	if((_flags & User) == 0 and (_flags & ReadOnly) == 0)
		_flags |= Archive;

	VariableMaster::the().register_variable(this);
}

Variable::Variable(std::string_view name, const glm::mat4 &value, BitMask32 flags) :
	_type(MAT4),
	_flags(flags)
{
	set_name(name);

	_current = value;
	_default = value;

	if((_flags & User) == 0 and (_flags & ReadOnly) == 0)
		_flags |= Archive;

	VariableMaster::the().register_variable(this);
}

Variable::~Variable()
{
	VariableMaster::the().deregister_variable(this);
}

void Variable::value2string(const value_t &value, std::string &s) const
{
	s.clear();

	switch(_type)
	{
	case INTEGER:
		s.reserve(17);
		std::format_to(std::back_inserter(s), "{}", std::get<int_t>(value));
		break;
	case REAL:
		s.reserve(17);
		std::format_to(std::back_inserter(s), "{}", std::get<real_t>(value));
		break;
	case STRING:
		s = std::get<std::string>(value);
		break;
	case VEC3:
	{
		const auto &v = std::get<glm::vec3>(value);
		s.reserve(32);
		std::format_to(std::back_inserter(s), "({}, {}, {})", v.x, v.y, v.z);
		break;
	}
	case VEC4:
	{
		const auto &v = std::get<glm::vec4>(value);
		s.reserve(32);
		std::format_to(std::back_inserter(s), "({}, {}, {}, {})", v.x, v.y, v.z, v.w);
		break;
	}
	case MAT4:
	{
		const auto &m = std::get<glm::mat4>(value);
		s.reserve(64);
		std::format_to(std::back_inserter(s), "( ({}, {}, {}, {}), ({}, {}, {}, {}), ({}, {}, {}, {}), ({}, {}, {}, {}) )",
					   m[0].x, m[0].y, m[0].z, m[0].w,
					   m[1].x, m[1].y, m[1].z, m[1].w,
					   m[2].x, m[2].y, m[2].z, m[2].w,
					   m[3].x, m[3].y, m[3].z, m[3].w);
		break;
	}
	case _StorageType_count:
		assert(false);
	}
}

void Variable::notify_subscribers()
{
	if(not _subscribers.empty())
	{
		Log::debug("Variable '{}'; notifying {} subscribers", _name, _subscribers.size());

		VariableMaster::the().notify_subscribers(this);
	}
}

// return the variable's value in string representation
std::string_view Variable::to_string() const
{
	if(_string_dirty)
	{
		value2string(_current, _as_string);
		if(not _as_string.empty())
			_string_dirty=false;
	}
	return _as_string;
}

std::string_view Variable::default_string() const
{
	if(_default_as_string.empty())
		value2string(_default, _default_as_string);
	return _default_as_string;
}

std::string Variable::flag_string() const
{
	std::string s;
	s.resize(5);

	s[0] = (_flags & Archive)?   'A': '-';
	s[1] = (_flags & Developer)? 'D': '-';
	s[2] = (_flags & ReadOnly)?  'R': '-';
	s[3] = (_flags & System)?    'S': '-';
	s[4] = (_flags & User)?      'U': '-';

	return s;
}

static std::pair<std::string_view, std::string_view> name_split(std::string_view name)
{
	if(auto dot = name.find('.'); dot != std::string_view::npos)
	{
		return {
			name.substr(0, dot),
			name.substr(dot + 1)
		};
	}
	return { {}, name };
}

void Variable::set_name(std::string_view name)
{
	assert(not name.empty() and not name.contains('\t') and not name.contains('\n'));

	_name = name;
	_group = {};
	_leaf_name = {};

	const auto [grp, leaf] = name_split(name);

	if(not grp.empty())
	{
		_group = grp;
		_leaf_name = leaf;
		assert(not _leaf_name.empty());
	}
	else
		_leaf_name = _name;
}

bool Variable::set(int_t value, bool notify)
{
	assert(_type == INTEGER);
	if(_type != INTEGER)
	{
		Log::warning("Variable '{}': not integer type: 'set' ignored", _name);
		return false;
	}

	if(_flags & ReadOnly)
	{
		Log::warning("Variable '{}': read only: 'set' ignored.", _name);
		return false;
	}

	_current = value;
	_string_dirty = true;

	if(notify)
		notify_subscribers();

	return true;
}

bool Variable::set(real_t value, bool notify)
{
	assert(_type == REAL);
	if(_type != REAL)
	{
		Log::warning("Variable '{}': not real type: 'set' ignored.", _name);
		return false;
	}

	if(_flags & ReadOnly)
	{
		Log::warning("Variable '{}': read only: 'set' ignored.", _name);
		return false;
	}

	_current = value;
	_string_dirty = true;

	if(notify)
		notify_subscribers();

	return true;
}

bool Variable::set(std::string_view value, bool notify)
{
	assert(_type == STRING);

	if(_type != STRING)
	{
		Log::warning("Variable '{}': not string type: 'set' ignored.", _name);
		return false;
	}

	if(_flags & ReadOnly)
	{
		Log::warning("Variable '{}': read only: 'set' ignored.", _name);
		return false;
	}

	_current = std::string(value);
	_string_dirty = true;

	if(notify)
		notify_subscribers();

	return true;
}

bool Variable::set(const glm::vec3 &value, bool notify)
{
	assert(_type == VEC3);

	if(_type != VEC3)
	{
		Log::warning("Variable '{}': not vec3 type: 'set' ignored.", _name);
		return false;
	}

	if(_flags & ReadOnly)
	{
		Log::warning("Variable '{}': read only: 'set' ignored.", _name);
		return false;
	}

	_current = value;
	_string_dirty = true;

	if(notify)
		notify_subscribers();

	return true;
}

bool Variable::set(const glm::vec4 &value, bool notify)
{
	assert(_type == VEC4);

	if(_type != VEC4)
	{
		Log::warning("Variable '{}': not vec4 type: 'set' ignored.", _name);
		return false;
	}

	if(_flags & ReadOnly)
	{
		Log::warning("Variable '{}': read only: 'set' ignored.", _name);
		return false;
	}

	_current = value;
	_string_dirty = true;

	if(notify)
		notify_subscribers();

	return true;
}

bool Variable::set(const glm::mat4 &value, bool notify)
{
	assert(_type == MAT4);

	if(_type != MAT4)
	{
		Log::warning("Variable '{}': not mat4 type: 'set' ignored.", _name);
		return false;
	}

	if(_flags & ReadOnly)
	{
		Log::warning("Variable '{}': read only: 'set' ignored.", _name);
		return false;
	}

	_current = value;
	_string_dirty = true;

	if(notify)
		notify_subscribers();

	return true;
}

void Variable::reset()
{
	_current = _default;
	_string_dirty = true; // string representation is now dirty

	notify_subscribers();
}

} // RGL