#include "spatial_allocator.h"
#include <boost/ut.hpp>

#include <chrono>
#include <print>
#include <format>


int main()
{
	using namespace RGL;
	using namespace boost::ut;

	using namespace std::chrono;

	const auto T0 = steady_clock::now();

	"ctor"_test = [] {
		SpatialAllocator a(8192);
		expect(a.size() == 8192);
		expect(a.num_allocated().empty()) << "nothing allocated";
		expect(a.max_size() == 1024);
		expect(a.min_size() == 128);
	};

	"ctor_rounding"_test = [] {
		SpatialAllocator a(300);
		expect(a.size() == 512) << std::to_string(a.size());
		expect(a.max_size() == 512 >> 3) << std::to_string(a.max_size());
		expect(a.min_size() == 512 >> 6) << std::to_string(a.min_size());
	};

	"num_allocatable"_test = [] {
		SpatialAllocator a1(1024, 64, 256);
		expect(a1.num_allocatable_levels() == 3) << "64, 128, 256";
		SpatialAllocator a2(8192, 64, 1024);
		expect(a2.num_allocatable_levels() == 5) << "64, 128, 256, 512, 1024";
		SpatialAllocator a3(8192, 1024, 1024);
		expect(a3.num_allocatable_levels() == 1) << "1024";
	};

	"allocate_1"_test = [] {
		SpatialAllocator a(1024, 64, 256);
		expect(a.num_allocated(256) == 0);
		auto node = a.allocate(256);
		expect(node != a.end()) << std::format("allocated not > {}", node);
		expect(a.num_allocated(256) == 1);
		a.free(node);
		expect(a.num_allocated(256) == 0);
	};

	"allocate_bad_size"_test = [] {
		SpatialAllocator a(1024, 64, 256);
		expect(a.allocate(512) == a.end());
		expect(a.allocate(32) == a.end());
	};

	"allocate_full"_test = [] {
		SpatialAllocator a(1024, 64, 256);
		for(auto idx = 0u; idx < 16; ++idx)
			expect(a.allocate(256) != a.end());
		expect(a.num_allocated().size() == 1);
		expect(a.num_allocated(256) == 16);
		expect(a.allocate(256) == a.end());
		expect(a.num_allocated().size() == 1);
		expect(a.num_allocated(256) == 16);
	};

	"allocate_demote"_test = [] {
		SpatialAllocator a(1024, 64, 256);
		for(auto idx = 0u; idx < 15; ++idx)
			expect(a.allocate(256) != a.end());
		expect(a.allocate(128) != a.end());
		auto demoted = a.allocate(256, 128);
		expect(demoted != a.end());
		expect(a.num_allocated().size() == 2); // 2 buckets used
		expect(a.num_allocated(256) == 15);
		expect(a.num_allocated(128) == 2);
		expect(a.rect(demoted).w == 128);
	};

	"allocate_after_free_many"_test = [] {
		SpatialAllocator a(1024, 64, 256);
		for(auto idx = 0u; idx < 15; ++idx)
			expect(a.allocate(256) != a.end());
		std::vector<decltype(a)::NodeIndex> small;
		for(auto idx = 0u; idx < 4; ++idx)
		{
			auto index = a.allocate(128);
			expect(index != a.end());
			small.push_back(index);
		}
		expect(a.allocate(256) == a.end());
		expect(a.allocate(64) == a.end());
		for(const auto &index: small)
			a.free(index);
		expect(a.allocate(256) != a.end());
	};

	"rects"_test = [] {
		SpatialAllocator a(8192);
		auto check_rect = [&a](auto node, const decltype(a)::Rect &re) {
			auto r = a.rect(uint32_t(node));
			expect(r.x == re.x) << std::format("rect[{}].x: {} != {}", node, r.x, re.x);
			expect(r.y == re.y) << std::format("rect[{}].y: {} != {}", node, r.y, re.y);
			expect(r.w == re.w) << std::format("rect[{}].w: {} != {}", node, r.w, re.w);
			expect(r.h == re.h) << std::format("rect[{}].h: {} != {}", node, r.h, re.h);
		};
		check_rect( 0, {    0,    0, 8192, 8192 });
		check_rect( 1, {    0,    0, 4096, 4096 });
		check_rect( 2, { 4096,    0, 4096, 4096 });
		check_rect( 3, {    0, 4096, 4096, 4096 });
		check_rect( 4, { 4096, 4096, 4096, 4096 });
		check_rect( 5, {    0,    0, 2048, 2048 });
		check_rect( 9, { 4096,    0, 2048, 2048 });
		check_rect(16, { 2048, 6144, 2048, 2048 });
		// more?
	};

	// "level"_test = [] {
	// 	SpatialAllocator a(8192);
	// 	expect(a.level(0) == 0);
	// 	expect(a.level(1) == 1);
	// 	expect(a.level(2) == 1);
	// 	expect(a.level(3) == 1);
	// 	expect(a.level(4) == 1);
	//
	// 	expect(a.level(6) == 2);
	//
	// 	expect(a.level(28) == 3);
	// };

	"size"_test = [] {
		SpatialAllocator a(8192);
		expect(a.size(0) == 8192);
		expect(a.size(1) == 4096);
		expect(a.size(2) == 4096);
		expect(a.size(3) == 4096);
		expect(a.size(4) == 4096);

		expect(a.size(6) == 2048);

		expect(a.size(28) == 1024);
	};

	// "parent_child"_test = [] {
	// 	SpatialAllocator a(8192);
	// 	expect(a.index_of_child(0, decltype(a)::NodeChild::TopLeft) == 1);
	// 	expect(a.index_of_child(0, decltype(a)::NodeChild::TopRight) == 2);
	// 	expect(a.index_of_child(0, decltype(a)::NodeChild::BottomLeft) == 3);
	// 	expect(a.index_of_child(0, decltype(a)::NodeChild::BottomRight) == 4);
	//
	// 	expect(a.index_of_child(6, decltype(a)::NodeChild::BottomRight) == 28);
	//
	// 	expect(a.index_of_parent(28) == 6);
	// 	expect(a.index_of_parent(4) == 0);
	// };

	// "node_child"_test = [] {
	// 	SpatialAllocator a(8192);
	// 	expect(a.child_of_parent(0) == decltype(a)::NodeChild::Invalid);
	//
	// 	expect(a.child_of_parent(1) == decltype(a)::NodeChild::TopLeft);
	// 	expect(a.child_of_parent(2) == decltype(a)::NodeChild::TopRight);
	// 	expect(a.child_of_parent(3) == decltype(a)::NodeChild::BottomLeft);
	// 	expect(a.child_of_parent(4) == decltype(a)::NodeChild::BottomRight);
	//
	// 	expect(a.child_of_parent(6) == decltype(a)::NodeChild::TopRight);
	//
	// 	expect(a.child_of_parent(28) == decltype(a)::NodeChild::BottomRight);
	// };


	const auto T1 = steady_clock::now();
	auto duration = T1 - T0;
	std::print("\x1b[32;1mTest duration:\x1b[m ");
	if(duration < 5000us)
		std::print("\x1b[97;1m{}\x1b[m\n", duration_cast<microseconds>(duration));
	else
		std::print("\x1b[97;1m{}\x1b[m\n", duration_cast<milliseconds>(duration));
}
