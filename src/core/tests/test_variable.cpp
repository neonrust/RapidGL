#include "variable.h"
#include "variable_master.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <boost/ut.hpp>
using namespace boost::ut;

using namespace RGL;
using namespace std::literals;

// NOTE: VariableMaster is a process-wide singleton, and variables (de)register themselves
//   on construction/destruction, so every test uses its own unique variable names.
//   Subscription prefixes must be < 16 characters (asserted in subscribe()).
//
// The syntax of Variable::to_string() is for presentation only, so only its behaviour is tested
//   (non-empty, updated on change), not its exact output. The output of VariableMaster::save() is tested exactly.


static_assert(not std::is_copy_constructible_v<Variable>);
static_assert(not std::is_copy_assignable_v<Variable>);
static_assert(not std::is_move_constructible_v<Variable>);
static_assert(not std::is_move_assignable_v<Variable>);


namespace
{

// a file in the system's temp directory, removed when going out of scope
struct TempFile
{
	explicit TempFile(std::string_view name) :
		path(std::filesystem::temp_directory_path() / std::format("rapidgl_test_{}.vars", name))
	{
		std::filesystem::remove(path);
	}
	~TempFile()
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}

	std::filesystem::path path;
};

std::vector<std::string> read_lines(const std::filesystem::path &path)
{
	std::vector<std::string> lines;
	std::ifstream in(path);
	for(std::string line; std::getline(in, line); )
		lines.push_back(std::move(line));
	return lines;
}

// save all variables and return the lines of the resulting file
std::vector<std::string> save_lines(std::string_view file_name)
{
	TempFile file(file_name);
	VariableMaster::the().save(file.path);
	return read_lines(file.path);
}

// the saved line of variable 'name' (empty if not found)
std::string find_line(const std::vector<std::string> &lines, std::string_view name)
{
	const auto prefix = std::format("{}\t", name);
	for(const auto &line: lines)
	{
		if(line.starts_with(prefix))
			return line;
	}
	return {};
}

void expect_saved(const std::vector<std::string> &lines, std::string_view name, std::string_view type, std::string_view value,
				  const reflection::source_location &location=reflection::source_location::current())
{
	const auto expected = std::format("{}\ttype:{}\tvalue:{}", name, type, value);
	const auto line = find_line(lines, name);
	expect(line == expected, location) << "\n  expected:" << expected << "\n  actual:  " << line;
}

} // anonymous


suite<fixed_string("Variable")> variable_suite([]{

	"integer"_test = [] {
		Variable v("tv_int", int_t{42});
		expect(v.type() == Variable::INTEGER);
		expect(v.is_integer());
		expect(v.integer() == 42);
		expect(v.value<int_t>() == 42);
		expect(not v.to_string().empty());
		expect(v.default_string() == v.to_string());
	};

	"real"_test = [] {
		Variable v("tv_real", 2.5f);
		expect(v.type() == Variable::REAL);
		expect(v.is_real());
		expect(v.real() == 2.5f);
		expect(v.value<real_t>() == 2.5f);
		expect(not v.to_string().empty());
		expect(v.default_string() == v.to_string());
	};

	"string"_test = [] {
		Variable v("tv_str", "hello");
		expect(v.type() == Variable::STRING);
		expect(v.is_string());
		expect(v.string() == "hello"sv);
		expect(v.value<std::string>() == "hello"sv);
		expect(not v.to_string().empty());
		expect(v.default_string() == v.to_string());
	};

	"vec3"_test = [] {
		Variable v("tv_vec3", glm::vec3{ 1, 2, 3 });
		expect(v.type() == Variable::VEC3);
		expect(v.is_vec3());
		expect(v.vec3() == glm::vec3{ 1, 2, 3 });
		expect(v.value<glm::vec3>() == glm::vec3{ 1, 2, 3 });
		expect(not v.to_string().empty());
		expect(v.default_string() == v.to_string());
	};

	"vec4"_test = [] {
		Variable v("tv_vec4", glm::vec4{ 1, 2, 3, 4 });
		expect(v.type() == Variable::VEC4);
		expect(v.is_vec4());
		expect(v.vec4() == glm::vec4{ 1, 2, 3, 4 });
		expect(v.value<glm::vec4>() == glm::vec4{ 1, 2, 3, 4 });
		expect(not v.to_string().empty());
		expect(v.default_string() == v.to_string());
	};

	"mat4"_test = [] {
		Variable v("tv_mat4", glm::mat4{ 2 });
		expect(v.type() == Variable::MAT4);
		expect(v.is_mat4());
		expect(v.mat4() == glm::mat4{ 2 });
		expect(v.value<glm::mat4>() == glm::mat4{ 2 });
		expect(not v.to_string().empty());
		expect(v.default_string() == v.to_string());
	};

	"name: grouped"_test = [] {
		Variable v("tvgrp.leaf", int_t{0});
		expect(v.name() == "tvgrp.leaf"sv);
		expect(v.has_group());
		expect(v.group() == "tvgrp"sv);
		expect(v.leaf_name() == "leaf"sv);
	};

	"name: leaf may contain dots"_test = [] {
		Variable v("tvgrp.sub.leaf", int_t{0});
		expect(v.name() == "tvgrp.sub.leaf"sv);
		expect(v.group() == "tvgrp"sv);
		expect(v.leaf_name() == "sub.leaf"sv);
	};

	"name: ungrouped"_test = [] {
		Variable v("tv_nogroup", int_t{0});
		expect(v.name() == "tv_nogroup"sv);
		expect(not v.has_group());
		expect(v.group().empty()) << "group:" << v.group();
		expect(v.leaf_name() == "tv_nogroup"sv);
	};

	"name: long (heap-allocated) name"_test = [] {
		Variable v("tv_long_group_name.and_a_rather_long_leaf_name", int_t{0});
		expect(v.name() == "tv_long_group_name.and_a_rather_long_leaf_name"sv);
		expect(v.group() == "tv_long_group_name"sv);
		expect(v.leaf_name() == "and_a_rather_long_leaf_name"sv);
	};

	"flags: archived by default"_test = [] {
		Variable v("tv_flags_def", int_t{0});
		expect((v.flags() & Variable::Archive) != 0);
	};

	"flags: user & read-only variables are not archived"_test = [] {
		Variable u("tv_flags_user", int_t{0}, Variable::User);
		expect((u.flags() & Variable::Archive) == 0);
		Variable r("tv_flags_ro", int_t{0}, Variable::ReadOnly);
		expect((r.flags() & Variable::Archive) == 0);
	};

	"flags: string"_test = [] {
		Variable v("tv_flags_str", int_t{0}, Variable::System | Variable::Developer);
		expect(v.flag_string() == "AD-S-"sv) << v.flag_string();
		Variable r("tv_flags_str_r", int_t{0}, Variable::ReadOnly | Variable::User);
		expect(r.flag_string() == "--R-U"sv) << r.flag_string();
	};

	"set: integer"_test = [] {
		Variable v("tv_set_int", int_t{1});
		const auto initial = std::string(v.to_string());   // also primes the cached string
		expect(v.set(int_t{2}));
		expect(v.integer() == 2);
		expect(v.to_string() != initial) << "string representation not updated";
		expect(v.default_string() == initial);
	};

	"set: real"_test = [] {
		Variable v("tv_set_real", 1.5f);
		const auto initial = std::string(v.to_string());
		expect(v.set(3.25f));
		expect(v.real() == 3.25f);
		expect(v.to_string() != initial) << "string representation not updated";
		expect(v.default_string() == initial);
	};

	"set: string"_test = [] {
		Variable v("tv_set_str", "hello");
		const auto initial = std::string(v.to_string());
		expect(v.set("world"sv));
		expect(v.string() == "world"sv);
		expect(v.to_string() != initial) << "string representation not updated";
		expect(v.default_string() == initial);
	};

	"set: vec3"_test = [] {
		Variable v("tv_set_vec3", glm::vec3{ 0 });
		const auto initial = std::string(v.to_string());
		expect(v.set(glm::vec3{ 1, 0.5f, 0.25f }));
		expect(v.vec3() == glm::vec3{ 1, 0.5f, 0.25f });
		expect(v.to_string() != initial) << "string representation not updated";
		expect(v.default_string() == initial);
	};

	"set: vec4"_test = [] {
		Variable v("tv_set_vec4", glm::vec4{ 0 });
		const auto initial = std::string(v.to_string());
		expect(v.set(glm::vec4{ 1, 0.5f, 0.25f, 0.75f }));
		expect(v.vec4() == glm::vec4{ 1, 0.5f, 0.25f, 0.75f });
		expect(v.to_string() != initial) << "string representation not updated";
		expect(v.default_string() == initial);
	};

	"set: mat4"_test = [] {
		Variable v("tv_set_mat4", glm::mat4{ 1 });
		const auto initial = std::string(v.to_string());
		expect(v.set(glm::mat4{ 2 }));
		expect(v.mat4() == glm::mat4{ 2 });
		expect(v.to_string() != initial) << "string representation not updated";
		expect(v.default_string() == initial);
	};

	"set: read-only is rejected"_test = [] {
		Variable v("tv_ro", int_t{1}, Variable::ReadOnly);
		expect(not v.set(int_t{2}));
		expect(v.integer() == 1);
	};

#ifdef NDEBUG   // set() deliberately asserts on type mismatch in debug builds
	"set: wrong type is rejected"_test = [] {
		Variable v("tv_wrongtype", int_t{1});
		expect(not v.set(2.5f));
		expect(not v.set("x"sv));
		expect(not v.set(glm::vec3{ 1 }));
		expect(not v.set(glm::mat4{ 1 }));
		expect(v.integer() == 1);
	};
#endif

	"reset restores default"_test = [] {
		Variable v("tv_reset", int_t{1});
		const auto initial = std::string(v.to_string());
		expect(v.set(int_t{5}));
		expect(v.to_string() != initial);
		v.reset();
		expect(v.integer() == 1);
		expect(v.to_string() == initial);
	};

	"conversion: string_view is the string representation"_test = [] {
		Variable i("tv_conv_int", int_t{7});
		expect(std::string_view(i) == i.to_string());

		const Variable s("tv_conv_str", "abc");   // also usable on a const variable
		expect(std::string_view(s) == s.to_string());
	};
});


suite<fixed_string("VariableMaster")> variable_master_suite([]{

	"registers on construction, deregisters on destruction"_test = [] {
		auto &vm = VariableMaster::the();
		const auto before = vm.num_variables();
		expect(not vm.contains("tvm_reg"));
		{
			Variable v("tvm_reg", int_t{0});
			expect(vm.num_variables() == before + 1);
			expect(vm.contains("tvm_reg"));
			const auto found = vm.find("tvm_reg");
			expect(found != vm.end() and found->second == &v);
		}
		expect(vm.num_variables() == before);
		expect(not vm.contains("tvm_reg"));
		expect(vm.find("tvm_reg") == vm.end());
	};

	"find unknown variable"_test = [] {
		auto &vm = VariableMaster::the();
		expect(not vm.contains("tvm_no_such_variable"));
		expect(vm.find("tvm_no_such_variable") == vm.end());
	};

	"iteration includes variable"_test = [] {
		auto &vm = VariableMaster::the();
		Variable v("tvm_iter", int_t{0});
		auto seen = 0;
		for(const auto &[name, var]: vm)
		{
			if(var == &v)
				++seen;
		}
		expect(seen == 1);
	};

	"grouped variable is found by its full name"_test = [] {
		auto &vm = VariableMaster::the();
		Variable v("tvmgrp.leaf", int_t{0});
		expect(vm.contains("tvmgrp.leaf"));
		const auto found = vm.find("tvmgrp.leaf");
		expect(found != vm.end() and found->second == &v);
		expect(not vm.contains("leaf"));
	};

	"same leaf name in different groups are distinct variables"_test = [] {
		auto &vm = VariableMaster::the();
		const auto before = vm.num_variables();
		{
			Variable a("tvmga.same", int_t{1});
			Variable b("tvmgb.same", int_t{2});
			expect(vm.num_variables() == before + 2);
			const auto found_a = vm.find("tvmga.same");
			expect(found_a != vm.end() and found_a->second == &a);
			const auto found_b = vm.find("tvmgb.same");
			expect(found_b != vm.end() and found_b->second == &b);
		}
		expect(vm.num_variables() == before);
		expect(not vm.contains("tvmga.same"));
		expect(not vm.contains("tvmgb.same"));
	};

	"subscribe: notified on change"_test = [] {
		auto &vm = VariableMaster::the();
		Variable v("tvm_sub_x", int_t{0});
		auto calls = 0;
		Variable *changed = nullptr;
		const auto id = vm.subscribe("tvm_sub_", [&](Variable *var) { ++calls; changed = var; });
		expect(v.set(int_t{1}));
		expect(calls == 1);
		expect(changed == &v);
		expect(v.set(int_t{2}));
		expect(calls == 2);
		vm.unsubscribe(id);
	};

	"subscribe: notified for every value type"_test = [] {
		auto &vm = VariableMaster::the();
		Variable i("tvm_types_i", int_t{0});
		Variable r("tvm_types_r", 0.f);
		Variable s("tvm_types_s", "");
		Variable v3("tvm_types_v3", glm::vec3{ 0 });
		Variable v4("tvm_types_v4", glm::vec4{ 0 });
		Variable m4("tvm_types_m4", glm::mat4{ 1 });
		auto calls = 0;
		const auto id = vm.subscribe("tvm_types_", [&](Variable *) { ++calls; });
		i.set(int_t{1});
		r.set(1.f);
		s.set("x"sv);
		v3.set(glm::vec3{ 1 });
		v4.set(glm::vec4{ 1 });
		m4.set(glm::mat4{ 2 });
		expect(calls == 6);
		vm.unsubscribe(id);
	};

	"subscribe: non-matching variable is not notified"_test = [] {
		auto &vm = VariableMaster::the();
		Variable other("tvm_nm_b1", int_t{0});
		Variable shorter("tvm_nm", int_t{0});   // shorter than the prefix
		auto calls = 0;
		const auto id = vm.subscribe("tvm_nm_a", [&](Variable *) { ++calls; });
		other.set(int_t{1});
		shorter.set(int_t{1});
		expect(calls == 0);
		vm.unsubscribe(id);
	};

	"subscribe: variable created after subscribing"_test = [] {
		auto &vm = VariableMaster::the();
		auto calls = 0;
		const auto id = vm.subscribe("tvm_late_", [&](Variable *) { ++calls; });
		{
			Variable v("tvm_late_x", int_t{0});
			v.set(int_t{1});
			expect(calls == 1);
		}
		{
			Variable v("tvm_late_x", int_t{0});   // re-created with the same name
			v.set(int_t{1});
			expect(calls == 2);
		}
		vm.unsubscribe(id);
	};

	"subscribe: grouped variable matches prefix of its full name"_test = [] {
		auto &vm = VariableMaster::the();
		Variable v("tvmsubgrp.x", int_t{0});
		Variable other("tvmothgrp.x", int_t{0});
		auto calls = 0;
		const auto id = vm.subscribe("tvmsubgrp.", [&](Variable *) { ++calls; });
		v.set(int_t{1});
		other.set(int_t{1});
		expect(calls == 1);
		vm.unsubscribe(id);
	};

	"subscribe: set without notify"_test = [] {
		auto &vm = VariableMaster::the();
		Variable v("tvm_quiet_x", int_t{0});
		auto calls = 0;
		const auto id = vm.subscribe("tvm_quiet_", [&](Variable *) { ++calls; });
		expect(v.set(int_t{1}, false));
		expect(v.integer() == 1);
		expect(calls == 0);
		vm.unsubscribe(id);
	};

	"subscribe: reset notifies"_test = [] {
		auto &vm = VariableMaster::the();
		Variable v("tvm_reset_x", int_t{0});
		auto calls = 0;
		const auto id = vm.subscribe("tvm_reset_", [&](Variable *) { ++calls; });
		v.set(int_t{5});
		expect(calls == 1);
		v.reset();
		expect(calls == 2);
		vm.unsubscribe(id);
	};

	"subscribe: all subscribers of the same prefix are notified"_test = [] {
		auto &vm = VariableMaster::the();
		Variable v("tvm_multi_x", int_t{0});
		auto calls_a = 0;
		auto calls_b = 0;
		const auto a = vm.subscribe("tvm_multi_", [&](Variable *) { ++calls_a; });
		const auto b = vm.subscribe("tvm_multi_", [&](Variable *) { ++calls_b; });
		expect(a != b);
		v.set(int_t{1});
		expect(calls_a == 1);
		expect(calls_b == 1);
		vm.unsubscribe(a);
		vm.unsubscribe(b);
	};

	"subscribe: overlapping prefixes"_test = [] {
		auto &vm = VariableMaster::the();
		Variable va("tvm_ov_a1", int_t{0});
		Variable vb("tvm_ov_b1", int_t{0});
		auto calls_short = 0;
		auto calls_long = 0;
		const auto s = vm.subscribe("tvm_ov_", [&](Variable *) { ++calls_short; });
		const auto l = vm.subscribe("tvm_ov_a", [&](Variable *) { ++calls_long; });
		va.set(int_t{1});
		expect(calls_short == 1);
		expect(calls_long == 1);
		vb.set(int_t{1});
		expect(calls_short == 2);
		expect(calls_long == 1);
		vm.unsubscribe(s);
		vm.unsubscribe(l);
	};

	"subscribe: empty prefix matches all variables"_test = [] {
		auto &vm = VariableMaster::the();
		Variable a("tvm_all_a", int_t{0});
		Variable b("other_all_b", int_t{0});
		auto calls = 0;
		const auto id = vm.subscribe("", [&](Variable *) { ++calls; });
		a.set(int_t{1});
		b.set(int_t{1});
		expect(calls == 2);
		vm.unsubscribe(id);
	};

	"unsubscribe: stops notifications"_test = [] {
		auto &vm = VariableMaster::the();
		Variable v("tvm_unsub_x", int_t{0});
		auto calls = 0;
		const auto id = vm.subscribe("tvm_unsub_", [&](Variable *) { ++calls; });
		v.set(int_t{1});
		expect(calls == 1);
		expect(vm.unsubscribe(id));
		v.set(int_t{2});
		expect(calls == 1);
	};

	"unsubscribe: other subscribers are kept"_test = [] {
		auto &vm = VariableMaster::the();
		Variable v("tvm_keep_x", int_t{0});
		auto calls_a = 0;
		auto calls_b = 0;
		const auto a = vm.subscribe("tvm_keep_", [&](Variable *) { ++calls_a; });
		const auto b = vm.subscribe("tvm_keep_", [&](Variable *) { ++calls_b; });
		expect(vm.unsubscribe(a));
		v.set(int_t{1});
		expect(calls_a == 0);
		expect(calls_b == 1);
		vm.unsubscribe(b);
	};

	"unsubscribe: after the variable is destroyed"_test = [] {
		auto &vm = VariableMaster::the();
		const auto id = vm.subscribe("tvm_gone_", [](Variable *) {});
		{
			Variable v("tvm_gone_x", int_t{0});
		}
		expect(vm.unsubscribe(id));
	};

#ifdef NDEBUG   // unsubscribe() deliberately asserts on unknown IDs in debug builds
	"unsubscribe: unknown id"_test = [] {
		auto &vm = VariableMaster::the();
		const auto id = vm.subscribe("tvm_twice_", [](Variable *) {});
		expect(vm.unsubscribe(id));
		expect(not vm.unsubscribe(id));
	};
#endif

	"callback may unsubscribe itself"_test = [] {
		auto &vm = VariableMaster::the();
		Variable v("tvm_self_x", int_t{0});
		auto calls_self = 0;
		auto calls_other = 0;
		SubscriberID self = 0;
		self = vm.subscribe("tvm_self_", [&](Variable *) {
			vm.unsubscribe(self);
			++calls_self;   // the callback must still be usable after unsubscribing
		});
		const auto other = vm.subscribe("tvm_self_", [&](Variable *) { ++calls_other; });
		v.set(int_t{1});
		expect(calls_self == 1);
		expect(calls_other == 1);
		v.set(int_t{2});
		expect(calls_self == 1);
		expect(calls_other == 2);
		vm.unsubscribe(other);
	};

	"callback may subscribe"_test = [] {
		auto &vm = VariableMaster::the();
		Variable v("tvm_add_x", int_t{0});
		auto calls_first = 0;
		auto calls_second = 0;
		auto calls_added = 0;
		SubscriberID added = 0;
		const auto first = vm.subscribe("tvm_add_", [&](Variable *) {
			if(++calls_first == 1)
				added = vm.subscribe("tvm_add_", [&](Variable *) { ++calls_added; });
		});
		const auto second = vm.subscribe("tvm_add_", [&](Variable *) { ++calls_second; });
		v.set(int_t{1});
		expect(calls_first == 1);
		expect(calls_second == 1);
		v.set(int_t{2});
		expect(calls_first == 2);
		expect(calls_second == 2);
		expect(calls_added == 1);
		vm.unsubscribe(first);
		vm.unsubscribe(second);
		vm.unsubscribe(added);
	};
});


suite<fixed_string("VariableMaster::save")> variable_save_suite([]{

	"save: returns true on success"_test = [] {
		Variable v("tvs.ok", int_t{1});
		TempFile file("ok");
		expect(VariableMaster::the().save(file.path));
		expect(std::filesystem::exists(file.path));
	};

	"save: integer, real & string"_test = [] {
		Variable i("tvs.int", int_t{-42});
		Variable r("tvs.real", 0.1f);
		Variable s("tvs.str", "hello world");
		Variable u("tvs_nogroup", int_t{7});
		const auto lines = save_lines("scalars");
		expect_saved(lines, "tvs.int",     "i", "-42");
		expect_saved(lines, "tvs.real",    "f", "0.1");
		expect_saved(lines, "tvs.str",     "s", "hello world");
		expect_saved(lines, "tvs_nogroup", "i", "7");
	};

	"save: vec3"_test = [] {
		Variable v("tvs.vec3", glm::vec3{ 1.5f, -2.25f, 1234.5f });
		const auto lines = save_lines("vec3");
		expect_saved(lines, "tvs.vec3", "v3", "1.5;-2.25;1234.5");
	};

	"save: vec4"_test = [] {
		Variable v("tvs.vec4", glm::vec4{ 0.5f, -1, 1234.5f, 0.125f });
		const auto lines = save_lines("vec4");
		expect_saved(lines, "tvs.vec4", "v4", "0.5;-1;1234.5;0.125");
	};

	"save: mat4 (column by column)"_test = [] {
		auto m = glm::mat4{ 1 };
		m[3] = glm::vec4{ 1234.5f, -0.25f, 8, 1 };   // translation
		Variable v("tvs.mat4", m);
		const auto lines = save_lines("mat4");
		expect_saved(lines, "tvs.mat4", "m44", "1;0;0;0;0;1;0;0;0;0;1;0;1234.5;-0.25;8;1");
	};

	"save: current value, not the default"_test = [] {
		Variable v("tvs.changed", int_t{1});
		v.set(int_t{2});
		const auto lines = save_lines("changed");
		expect_saved(lines, "tvs.changed", "i", "2");
	};

	"save: only archived variables"_test = [] {
		Variable archived("tvs.archived", int_t{1});
		Variable user("tvs.user", int_t{1}, Variable::User);
		Variable read_only("tvs.read_only", int_t{1}, Variable::ReadOnly);
		const auto lines = save_lines("archived");
		expect(not find_line(lines, "tvs.archived").empty());
		expect(find_line(lines, "tvs.user").empty());
		expect(find_line(lines, "tvs.read_only").empty());
	};

	// the value is last on the line, so it may contain tabs (newlines are asserted against)
	"save: string value containing a tab"_test = [] {
		Variable v("tvs.tab", "a\tb");
		const auto lines = save_lines("tab");
		expect_saved(lines, "tvs.tab", "s", "a\tb");
	};

	"save: every line is a complete record"_test = [] {
		Variable i("tvs.rec_int", int_t{1});
		Variable m("tvs.rec_mat4", glm::mat4{ 1 });
		const auto lines = save_lines("records");
		expect(not lines.empty());
		for(const auto &line: lines)
		{
			const auto type_pos = line.find("\ttype:");
			const auto value_pos = line.find("\tvalue:");
			expect(type_pos != std::string::npos and type_pos > 0 and value_pos != std::string::npos and value_pos > type_pos)
				<< "not a variable record:" << line;
		}
	};

	"save: unwritable path returns false"_test = [] {
		Variable v("tvs.unwritable", int_t{1});
		expect(not VariableMaster::the().save("/nonexistent-directory/variables.vars"));
	};
});
