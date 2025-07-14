cmake_minimum_required(VERSION 3.20)

project(core_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

set(TEST_SOURCE_FILES
	test_spatial_allocator.cpp
)

add_executable(core_tests ${TEST_SOURCE_FILES})

target_link_libraries(core_tests PRIVATE Boost::ut ${CORE_LIB_NAME})

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
