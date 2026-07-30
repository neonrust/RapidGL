#pragma once

#include <filesystem>
#include <string_view>

#include "container_types.h"

namespace RGL
{
class Variable;


class VariableMaster
{
private:
	using VariableMap = string_map<Variable *>;
public:
	using iterator       = VariableMap::iterator;
	using const_iterator = VariableMap::const_iterator;

	friend class Variable;

public:
	static VariableMaster *the();
	~VariableMaster();

	bool contains(std::string_view name);

	iterator findVariable(std::string_view name);

	bool load(const std::filesystem::path &filename, std::string_view pattern={});
	bool save(const std::filesystem::path &filename, std::string_view pattern={}) const;

	uint32_t numVariables() const;
	const iterator begin();
	const iterator end();

private:
	VariableMaster();   // singelton class

	void registerVariable(Variable *var);
	void deregisterVariable(Variable *var);
	void variableChanged(Variable *var);


private:
	VariableMap _variables;
	string_map<VariableMap> _variableGroups;
};

}; // RGL
