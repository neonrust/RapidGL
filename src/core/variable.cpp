#include "variable.h"
#include "variable_master.h"

#include <format>
#include <print>


using namespace RGL;


// create Integer variable
Variable::Variable(std::string_view name, int_t value, BitMask32 flags) :
	_type(INTEGER),
	_flags(flags)
{
	set_name(name);

	_current = value;
	_default = value;

	if((_flags & User) == 0 && (_flags & ReadOnly) == 0)
		_flags |= Archive;
	
	VariableMaster::the()->registerVariable(this);
}

// create Float variable
Variable::Variable(std::string_view name, real_t value, BitMask32 flags) :
	_type(REAL),
	_flags(flags)
{
	set_name(name);

	_current = value;
	_default = value;

	if((_flags & User) == 0 && (_flags & ReadOnly) == 0)
		_flags |= Archive;

	VariableMaster::the()->registerVariable(this);
}

// create String variable
Variable::Variable(std::string_view name, std::string_view value, BitMask32 flags) :
	_type(STRING),
	_flags(flags)
{
	set_name(name);

	_current = std::string(value);
	_default = std::string(value);
	
	if((_flags & User) == 0 && (_flags & ReadOnly) == 0)
		_flags |= Archive;

	VariableMaster::the()->registerVariable(this);
}

// create String variable
Variable::Variable(std::string_view name, const ColorRgba &value, BitMask32 flags) :
	_type(RGB),
	_flags(flags)
{
	set_name(name);

	_current = value;
	_default = value;

	if((_flags & User) == 0 && (_flags & ReadOnly) == 0)
		_flags |= Archive;

	VariableMaster::the()->registerVariable(this);
}

Variable::~Variable()
{
	VariableMaster::the()->deregisterVariable(this);
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
		s.reserve(std::get<std::string>(value).size());
		s = std::get<std::string>(value);
		break;
	case RGB:
	{
		const auto &c = std::get<ColorRgb>(value);
		s.reserve(32);
		std::format_to(std::back_inserter(s), "({:.2f}, {:.2f}, {:.2f})", c.r, c.g, c.b);
	}
	break;
	case RGBA:
	{
		const auto &c = std::get<ColorRgba>(value);
		s.reserve(32);
		std::format_to(std::back_inserter(s), "({:.2f}, {:.2f}, {:.2f}, {:.2f})", c.r, c.g, c.b, c.a);
	}
	break;
	case VEC3:
		break;
	case VEC4:
		break;
	case MAT4:
		break;

	}
}

// return the variable's value in string representation
std::string_view Variable::toString() const
{
	if(_stringDirty)
	{
		value2string(_current, _asString);
		if(not _asString.empty())
			_stringDirty=false;
	}
	return _asString;
}

std::string_view Variable::defaultAsString() const
{
	if(_defaultAsString.empty())
		value2string(_default, _defaultAsString);
	return _defaultAsString;
}

std::string Variable::flagsString() const
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

std::pair<std::string_view, std::string_view> Variable::name_split(std::string_view name)
{
	if(auto dot = name.find('.'); dot > 0)
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
	assert(not name.empty());

	const auto &[g, n] = name_split(name);

	if(not g.empty())
	{
		_group = std::string(g);
		_name = std::string(n);
		assert(not _name.empty());
	}
	else
	{
		_name = std::string(n);
		if(_group.capacity() > 0)
		{
			// deallocate '_group'
			std::string empty;
			_group.swap(empty);
		}
	}
}


// bool Variable::operator == (int comp)
// {
// 	if(_type==INTEGER)
// 		return _current.intValue==comp;
// 	return false;
// }

// bool Variable::operator == (float comp)
// {
// 	if(_type==FLOAT)
// 		return _current.floatValue==comp;
// 	return false;
// }
	
// bool Variable::operator == (const char *comp)
// {
// 	if(_type==STRING)
// 		return strcmp(_current.stringValue, comp)==0;
// 	return false;
// }

// bool Variable::operator == (const Color &comp)
// {
// 	if(_type==RGB)
// 		return _current.colorValue->operator==(comp);
// 	return false;
// }

Variable::operator int_t ()
{
	switch(_type)
	{
	case INTEGER:
		return std::get<int_t>(_current);
	case REAL:
		return int64_t(std::get<real_t>(_current));
	case STRING:
	case RGB:
	case RGBA:
	case VEC3:
	case VEC4:
	case MAT4:
		return 0;   // not really useful
	}
	return 0;
}

Variable::operator std::string_view ()
{
	if(_type == STRING)
		return std::get<std::string>(_current);
	else
		return toString();
}

Variable::operator real_t ()
{
	if(_type == REAL)
		return std::get<real_t>(_current);
	else if(_type == INTEGER)
		return real_t(std::get<int_t>(_current));
	return real_t{0};
}

Variable::operator ColorRgba ()
{
	if(_type == RGBA)
		return std::get<ColorRgba>(_current);
	else if(_type == RGB)
	{
		const auto &c = std::get<ColorRgb>(_current);
		return ColorRgba{c.r, c.g, c.b, 0.f};
	}
	return ColorRgba{0, 0, 0, 0};
}

void Variable::reset()
{
	_current = _default;
	_stringDirty = true; // string representation is now dirty
}

bool Variable::set(int_t value, bool notify)
{
	if(_type != INTEGER)
	{
		std::println("Variable '{}' is not of integer type: 'set' ignored", _name);
		return false;
	}

	if(_flags & ReadOnly)
	{
		std::println("Variable '{}' is read only: 'set' ignored.", _name);
		return false;
	}

	_current = value;
	_stringDirty = true;

	// TRACE_VAR2("set '{}' = {}\n", _name, value);

	if(notify)
		VariableMaster::the()->variableChanged(this);

	return true;
}

bool Variable::set(real_t value, bool notify)
{
	if(_type != REAL)
	{
		std::println("Variable '{}' is not of real type: 'set' ignored.", _name);
		return false;
	}

	if(_flags & ReadOnly)
	{
		std::println("Variable '{}' is read only: 'set' ignored.", _name);
		return false;
	}

	_current = value;
	_stringDirty = false;

	// TRACE_VAR2("set '{}' = {}\n", _name, value);

	if(notify)
		VariableMaster::the()->variableChanged(this);

	return true;
}

bool Variable::set(std::string_view value, bool notify)
{
	if(_type != STRING)
	{
		std::println("Variable '{}' is not of string type: 'set' ignored.", _name);
		return false;
	}

	if(_flags & ReadOnly)
	{
		std::println("Variable '{}' is read only: 'set' ignored.", _name);
		return false;
	}

	_current = std::string(value);
	_stringDirty = false;

	// TRACE_VAR2("set '{}' = '{}'\n", _name, value);

	if(notify)
		VariableMaster::the()->variableChanged(this);

	return true;
}

bool Variable::set(const ColorRgba &value, bool notify)
{
	if(_type != RGBA)
	{
		std::println("Variable '{}' is not of RGBA type: 'set' ignored.", _name);
		return false;
	}

	if(_flags & ReadOnly)
	{
		std::println("Variable '{}' is read only: 'set' ignored.", _name);
		return false;
	}

	_current = value;
	_stringDirty = false;

	// TRACE_VAR2("set '{}' = {}\n", _name, toString());

	if(notify)
		VariableMaster::the()->variableChanged(this);

	return true;
}
