#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <variant>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>


namespace RGL
{
// forward declarations
class VariableMaster;
class Variable;

using int_t = int64_t;
using real_t = float;
struct ColorRgb
{
	float r, g, b;
	inline float operator [] (auto idx) {
		assert(idx >= 0 and idx <= 2);
		// return *(&r[idx]);
		switch(idx)
		{
		case 0: return r;
		case 1: return g;
		case 2: return b;
		}
	}
};
struct ColorRgba
{
	float r, g, b, a { 1 };
	inline float operator [] (auto idx) {
		assert(idx >= 0 and idx <= 3);
		// return *(&r[idx]);
		switch(idx)
		{
		case 0: return r;
		case 1: return g;
		case 2: return b;
		case 3: return a;
		}
	}
};
using BitMask32 = uint32_t;


class Variable
{
	friend class VariableMaster;
	
public:
	//! supported variable storage types
	enum StorageType
	{
		INTEGER = 0,
		REAL,
		STRING,
		RGB,
		RGBA,
		VEC3,
		VEC4,
		MAT4,
		_StorageTypeCount
	};

	// bitmask flags returned by getFlags()
	static constexpr BitMask32 System    = 0x0001;  // only modifiable by the system
	static constexpr BitMask32 User      = 0x0002;  // created by the user
	static constexpr BitMask32 Archive   = 0x0004;  // will be saved when exiting
	static constexpr BitMask32 Developer = 0x0008;  // only modifiable in devloper mode
	static constexpr BitMask32 ReadOnly  = 0x0010;  // variable can not be modified

	static constexpr BitMask32 DefaultFlags = 0;

public:
	// 'name' either "name" or "group.name"
	Variable(std::string_view name, int_t value,            BitMask32 flags=DefaultFlags);
	Variable(std::string_view name, real_t value,           BitMask32 flags=DefaultFlags);
	Variable(std::string_view name, std::string_view value, BitMask32 flags=DefaultFlags);
	Variable(std::string_view name, const ColorRgba &value, BitMask32 flags=DefaultFlags);
	~Variable();

	inline std::string_view name() const { return _name; }
	inline std::string_view group() const { return _group; }

	bool set(int_t value, bool notify=true);
	bool set(real_t value, bool notify=true);
	bool set(std::string_view value, bool notify=true);
	bool set(const ColorRgba &value, bool notify=true);
	
// 	int operator = (int value);
// 	float operator = (float value);
// 	const char *operator = (const char *value);
// 	const Color &operator = (const Color &value);

// 	bool operator == (int comp);
// 	bool operator == (float comp);
// 	bool operator == (const char *comp);
// 	bool operator == (const Color &comp);
	
	inline StorageType type() const { return _type; }
	inline bool isInteger() const  { return type() == INTEGER; }
	inline bool isReal() const     { return type() == REAL; }
	inline bool isString() const   { return type() == STRING; }
	inline bool isColor() const    { return type() == RGB; }

	int64_t integer() const;         // only valid for Integer variables of type integer
	real_t real() const;             // only valid for variables of type real
	std::string_view string() const; // only valid for variables of type string
	const ColorRgba &rgba() const;      // only valid for variables of type string


	std::string_view toString() const;

	operator int64_t ();
	operator real_t ();
	operator std::string_view ();
	operator ColorRgba ();

	std::string_view defaultAsString() const;
	void reset();   // (re)set current value to the initial value

	inline BitMask32 flags() const { return _flags; }
	// 	void setFlags(int flags); // modify the flag settings (e.g. flagArchive)
	std::string flagsString() const;

private:
	static std::pair<std::string_view, std::string_view> name_split(std::string_view name);
	void set_name(std::string_view name);

	using value_t = std::variant<std::monostate, int_t, real_t, std::string, ColorRgb, ColorRgba, glm::vec3, glm::vec4, glm::mat4>;

	void value2string(const value_t &value, std::string &s) const;

private:
	std::string _name;
	std::string _group;
	StorageType _type;
	BitMask32 _flags;

	value_t _current;
	value_t _default;

	mutable std::string _asString;   // string representation of the current value
	mutable bool _stringDirty { true };       // whether the '_asString' ineeds to regenerated
	mutable std::string _defaultAsString;
};

inline int_t Variable::integer() const
{
	assert(isInteger());
	return std::get<int_t>(_current);
}

inline real_t Variable::real() const
{
	assert(isReal());
	return std::get<real_t>(_current);
}

inline std::string_view Variable::string() const
{
	assert(isString());
	return std::get<std::string>(_current);
}

inline const ColorRgba &Variable::rgba() const
{
	assert(isColor());
	return std::get<ColorRgba>(_current);
}

}; // RGL

