#include "ringbuffer.h"

#include <array>
#include <ranges>

#include <boost/ut.hpp>
using namespace boost::ut;

static_assert(std::ranges::range<RingBuffer<int, 4>>);

static_assert(std::bidirectional_iterator<RingBuffer<int, 4>::iterator>);
static_assert(std::bidirectional_iterator<RingBuffer<int, 4>::const_iterator>);
static_assert(std::ranges::forward_range<RingBuffer<int, 4>>);
static_assert(std::ranges::forward_range<const RingBuffer<int, 4>>);


suite<fixed_string("RingBuffer")> rb_suite([]{

	auto expect_range = []<typename T>(const auto &r, const std::initializer_list<T> &values) {
		for(const auto &[idx, v]: std::views::enumerate(r))
			expect(v == *(values.begin() + idx));
	};

	"partial"_test = [] {
		RingBuffer<int, 4> r;
		expect(r.size() == 0);
		expect(r.empty());
		expect(not r.full());
		r.push(1);
		expect(r.head() == 1);
		expect(r.tail() == 1);
		r.push(2);
		expect(r.head() == 2);
		expect(r.tail() == 1);
		expect(r.size() == 2);
		expect(not r.empty());
		expect(not r.full());
	};

	"full"_test = [] {
		RingBuffer<int, 4> r;
		r.push(1);
		r.push(2);
		r.push(3);
		expect(r.size() == 3);
		expect(not r.empty());
		expect(not r.full());
		r.push(4);
		expect(r.size() == 4);
		expect(not r.empty());
		expect(r.full());
	};

	"overflow"_test = [&expect_range] {
		RingBuffer<int, 4> r;
		for(auto idx = 0; idx < 4; ++idx)
			r.push(100 + idx);
		r.push(42);
		expect(r.size() == 4);
		expect(not r.empty());
		expect(r.full());
		expect(r.tail() == 101);
		expect(r.head() == 42);
		expect_range(r, { 101, 102, 103, 42 });
	};

	"overflow_many"_test = [&expect_range] {
		RingBuffer<int, 4> r;
		for(auto idx = 0; idx < 30; ++idx)
			r.push(100 + idx);
		r.push(42);
		r.push(43);
		r.push(44);
		expect(r.size() == 4);
		expect(not r.empty());
		expect(r.full());
		expect(r.tail() == 129);
		expect(r.head() == 44);
		expect_range(r, { 129, 42, 43, 44 });
	};

	"push_list"_test = [] {
		RingBuffer<int, 4> r;
		r.push({ 42, 43, 44, 45 });
		expect(r.size() == 4);
		expect(not r.empty());
		expect(r.full());
	};

	"push_list_overflow"_test = [&expect_range] {
		RingBuffer<int, 4> r;
		r.push({ 42, 43, 44, 45, 47, 48, 49, 50, 51, 52, 53 });
		expect(r.size() == 4);
		expect(not r.empty());
		expect(r.full());
		expect(r.tail() == 50);
		expect(r.head() == 53);
		expect_range(r, { 50, 51, 52, 53 });
	};

	"empty_oob_at"_test = [] {
		RingBuffer<int, 4> r;
		const std::array<int, 4> a { 0, 1, 2, 3 };
		expect(throws<std::out_of_range>([&r]() {
			auto _ = r.at(0);
		}));
	};

	"partial_oob_at"_test = [] {
		RingBuffer<int, 4> r;
		r.push(42);
		r.push(43);
		expect(r[0] == 42);
		expect(r[1] == 43);
		expect(throws<std::out_of_range>([&r]() {
			auto _ = r.at(2);
		}));
	};

	"overflow_oob_at"_test = [] {
		RingBuffer<int, 4> r;
		for(auto idx = 0; idx < 30; ++idx)
			r.push(100 + idx);
		r.push(42);
		r.push(43);
		r.push(44);
		expect(r[0] == 129);
		expect(r[1] == 42);
		expect(r[2] == 43);
		expect(r[3] == 44);
		expect(throws<std::out_of_range>([&r]() {
			auto _ = r.at(4);
		}));
	};

	"empty_iter"_test = [] {
		RingBuffer<int, 4> r;
		expect(r.begin() == r.end());
		for(const auto &_: r)
			expect(false);
	};

	"push_pop_tail"_test = [] {
		RingBuffer<int, 4> r;
		r.push(42);
		expect(r.size() == 1);
		r.pop_tail();
		expect(r.empty());
	};

	"push_pop_head"_test = [] {
		RingBuffer<int, 4> r;
		r.push(42);
		expect(r.size() == 1);
		r.pop_head();
		expect(r.empty());
	};

	"push2_pop_tail"_test = [] {
		RingBuffer<int, 4> r;
		r.push(42);
		r.push(123);
		expect(r.size() == 2);
		r.pop_tail();
		expect(r.size() == 1);
		expect(r.tail() == 123);
		expect(r.head() == 123);
	};

	"push2_pop_jead"_test = [] {
		RingBuffer<int, 4> r;
		r.push(42);
		r.push(123);
		expect(r.size() == 2);
		r.pop_head();
		expect(r.size() == 1);
		expect(r.tail() == 42);
		expect(r.head() == 42);
	};

	"full_pop_tail"_test = [] {
		RingBuffer<int, 4> r;
		r.push({ 42, 43, 44, 45 });
		expect(r.head() == 45);
		expect(r.size() == 4);
		r.pop_tail();
		expect(not r.empty());
		expect(r.size() == 3);
		expect(r.tail() == 43);
		expect(r.head() == 45);
	};

	"full_pop_head"_test = [] {
		RingBuffer<int, 4> r;
		r.push({ 42, 43, 44, 45 });
		expect(r.head() == 45);
		expect(r.size() == 4);
		r.pop_head();
		expect(not r.empty());
		expect(r.size() == 3);
		expect(r.tail() == 42);
		expect(r.head() == 44);
	};

	"iter_empty"_test = [] {
		RingBuffer<int, 4> r;
		for([[maybe_unused]] const auto &[idx, value]: std::views::enumerate(r))
			expect(false);
	};

	"iter_full"_test = [] {
		RingBuffer<int, 4> r;
		r.push({ 42, 43, 44, 45 });
		std::array<int, 4> expected = { 42, 43, 44, 45 };
		for(const auto &[idx, value]: std::views::enumerate(r))
			expect(expected[size_t(idx)] == value);
	};

	"iter_overflow"_test = [] {
		RingBuffer<int, 4> r;
		r.push({ 42, 43, 44, 45, 46, 47 });
		std::array<int, 4> expected = { 44, 45, 46, 47 };
		for(const auto &[idx, value]: std::views::enumerate(r))
			expect(expected[size_t(idx)] == value);
	};

	"iter_nonfull"_test = [] {
		RingBuffer<int, 32> r;
		r.push({ 42, 43, 44, 45 });
		std::array<int, 4> expected = { 42, 43, 44, 45 };
		for(const auto &[idx, value]: std::views::enumerate(r))
			expect(expected[size_t(idx)] == value);
	};

});
