#pragma once

#include <filesystem>
#include <string_view>

#include "container_types.h"

namespace RGL
{
class Variable;

using SubscriberID = uint32_t;

class VariableMaster
{
private:
	using VariableMap = string_map<Variable *>;
public:
	using iterator       = VariableMap::iterator;
	using const_iterator = VariableMap::const_iterator;

	friend class Variable;

public:
	static VariableMaster &the();
	~VariableMaster();

	bool contains(std::string_view name);

	iterator find(std::string_view name);

	bool load(const std::filesystem::path &filename, std::string_view pattern={});
	bool save(const std::filesystem::path &filename, std::string_view pattern={}) const;

	uint32_t num_variables() const;
	const iterator begin();
	const iterator end();

	using VariableChangedCB = std::function<void(Variable *)>;
	SubscriberID subscribe(std::string_view name_prefix, VariableChangedCB callback);
	bool unsubscribe(SubscriberID id);

private:
	VariableMaster();   // singelton class

	void register_variable(Variable *var);
	void deregister_variable(Variable *var);

	void notify_subscribers(Variable *var);

private:
	VariableMap _variables;
	string_map<VariableMap> _variable_groups;

	SubscriberID _next_subscriber_id { 1 };
	struct Subscription
	{
		std::string name_prefix;
		VariableChangedCB callback;
	};
	dense_map<SubscriberID, Subscription> _subscribers;
};

}; // RGL
