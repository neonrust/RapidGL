#include "ringbuffer.h"

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

	"add_full"_test = [] {
		RingBuffer<int, 4> r;
		expect(r.size() == 0);
		expect(r.empty());
		expect(not r.full());
		r.push(1);
		expect(r.size() == 1);
		expect(not r.empty());
		expect(not r.full());
		r.push(2);
		expect(r.size() == 2);
		expect(not r.empty());
		expect(not r.full());
		r.push(3);
		expect(r.size() == 3);
		expect(not r.empty());
		expect(not r.full());
		r.push(4);
		expect(r.size() == 4);
		expect(not r.empty());
		expect(r.full());
	};

	"add_overflow"_test = [] {
		RingBuffer<int, 4> r;
		for(auto idx = 0; idx < 4; ++idx)
			r.push(100 + idx);
		r.push(42);
		expect(r.size() == 4);
		expect(not r.empty());
		expect(r.full());
		static const int expected[] = { 101, 102, 103, 42 };
		auto idx = 0u;
		for(const auto &v:r)
			expect(v == expected[idx++]);
	};

	"add_overflow_many"_test = [&expect_range] {
		RingBuffer<int, 4> r;
		for(auto idx = 0; idx < 30; ++idx)
			r.push(100 + idx);
		r.push(42);
		r.push(43);
		r.push(44);
		r.push(45);
		expect(r.size() == 4);
		expect(not r.empty());
		expect(r.full());
		expect_range(r, { 42, 43, 44, 45 });
	};

	// TODO: more
});
