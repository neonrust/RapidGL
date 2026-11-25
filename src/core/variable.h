#pragma once

#include "container_types.h"
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

using SubscriberID = uint32_t;
using int_t        = int64_t;
using real_t       = float;
using BitMask32    = uint32_t;

using VariableValue = std::variant<int_t, real_t, std::string, glm::vec3, glm::vec4, glm::mat4>;;

class Variable
{
	friend class VariableMaster;
	
public:
	using SubscriberID = uint32_t; // hash of prefix & index into vector ?

	//! supported variable storage types
	enum StorageType
	{
		INTEGER = 0,
		REAL,
		STRING,
		VEC3,
		VEC4,
		MAT4,
		_StorageType_count
	};

	// bitmask flags returned by getFlags()
	static constexpr BitMask32 System    = 0x0001;  // only modifiable by the system (not the end user)
	static constexpr BitMask32 User      = 0x0002;  // created by the end user
	static constexpr BitMask32 Archive   = 0x0004;  // will be saved when exiting
	static constexpr BitMask32 Developer = 0x0008;  // only modifiable in developer mode
	static constexpr BitMask32 ReadOnly  = 0x0010;  // variable can not be modified

	static constexpr BitMask32 DefaultFlags = 0;

public:
	// `name` either "<name>" or "<group>.<name>" (where <name> can also contain '.')
	Variable(std::string_view name, int_t value,            BitMask32 flags=DefaultFlags);
	Variable(std::string_view name, real_t value,           BitMask32 flags=DefaultFlags);
	Variable(std::string_view name, std::string_view value, BitMask32 flags=DefaultFlags);
	Variable(std::string_view name, const glm::vec3 &value, BitMask32 flags=DefaultFlags);
	Variable(std::string_view name, const glm::vec4 &value, BitMask32 flags=DefaultFlags);
	Variable(std::string_view name, const glm::mat4 &value, BitMask32 flags=DefaultFlags);
	~Variable();

	// non-copyable & non-movable: VariableMaster refers to variables by address,
	//   and '_group' & '_leaf_name' are views into '_name'
	Variable(const Variable &) = delete;
	Variable(Variable &&) = delete;
	Variable &operator = (const Variable &) = delete;
	Variable &operator = (Variable &&) = delete;

	inline std::string_view name() const  { return _name; }
	inline std::string_view group() const { return _group; }
	inline bool has_group() const         { return not _group.empty(); }
	inline std::string_view leaf_name() const { return _leaf_name; }

	bool set(int_t value, bool notify=true);
	bool set(real_t value, bool notify=true);
	bool set(std::string_view value, bool notify=true);
	bool set(const glm::vec3 &value, bool notify=true);
	bool set(const glm::vec4 &value, bool notify=true);
	bool set(const glm::mat4 &value, bool notify=true);
	void reset();   // (re)set value to its initial value

	inline StorageType type() const { return _type; }
	inline bool is_integer() const      { return type() == INTEGER; }
	inline bool is_real() const         { return type() == REAL; }
	inline bool is_string() const       { return type() == STRING; }
	inline bool is_vec3() const         { return type() == VEC3; }
	inline bool is_vec4() const         { return type() == VEC4; }
	inline bool is_mat4() const         { return type() == MAT4; }

	int64_t integer() const;          // only valid variables of type integer
	real_t real() const;              // only valid variables of type real
	std::string_view string() const;  // only valid variables of type string
	const glm::vec3 &vec3() const;    // only valid variables of type vec3
	const glm::vec4 &vec4() const;    // only valid variables of type vec4
	const glm::mat4 &mat4() const;    // only valid variables of type mat4

	template<typename T> // TODO: requires one of the supported types
	const T &value() const { return std::get<T>(_current); }

	std::string_view to_string() const;
	inline operator std::string_view () const { return to_string(); }

	std::string_view default_string() const;
	inline BitMask32 flags() const { return _flags; }
	// 	void setFlags(int flags); // modify the flag settings (e.g. flagArchive)
	std::string flag_string() const;

private:
	void set_name(std::string_view name);

	using value_t = VariableValue;
	void value2string(const value_t &value, std::string &s) const;

	void notify_subscribers();

private:
	std::string _name;
	std::string_view _group;
	std::string_view _leaf_name;
	StorageType _type;
	BitMask32 _flags;

	value_t _current;
	value_t _default;

	mutable std::string _as_string;   // string representation of the current value
	mutable bool _string_dirty { true };       // whether the '_asString' ineeds to regenerated
	mutable std::string _default_as_string;

	dense_set<SubscriberID> _subscribers; // used by VariableMaster
};

inline int_t Variable::integer() const
{
	assert(is_integer());
	return std::get<int_t>(_current);
}

inline real_t Variable::real() const
{
	assert(is_real());
	return std::get<real_t>(_current);
}

inline std::string_view Variable::string() const
{
	assert(is_string());
	return std::get<std::string>(_current);
}

inline const glm::vec3 &Variable::vec3() const
{
	assert(is_vec3());
	return std::get<glm::vec3>(_current);
}

inline const glm::vec4 &Variable::vec4() const
{
	assert(is_vec4());
	return std::get<glm::vec4>(_current);
}

inline const glm::mat4 &Variable::mat4() const
{
	assert(is_mat4());
	return std::get<glm::mat4>(_current);
}

}; // RGL

