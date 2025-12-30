# Copyright (C) 2022 Tomasz Gałaj

set(DEMO_NAME "27_clustered_shading")

# Add source files
set(SOURCE_FILES_EXE
	clustered_shading.cpp
	clustered_shading_debug_draw.cpp
	clustered_shading_ui.cpp
	main.cpp
)

# Add header files
set(HEADER_FILES_EXE
	clustered_shading.h
)


file(GLOB_RECURSE SHADER_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/*.frag
	${CMAKE_CURRENT_SOURCE_DIR}/*.vert
	${CMAKE_CURRENT_SOURCE_DIR}/*.comp
	${CMAKE_CURRENT_SOURCE_DIR}/*.glh
)
set(SHADER_FILES
	shaders/cubemap.vert
	shaders/equirectangular_to_cubemap.frag
	shaders/gaussian_blur_lds.comp
	shaders/gaussian_blur_parametric.comp
	shaders/irradiance_convolution.frag
	shaders/precompute_brdf.frag
	shaders/prefilter_cubemap.frag
	shaders/shadow_cube.frag
	shaders/shadow_cube.vert
)

# Define the executable
add_executable(${DEMO_NAME} ${HEADER_FILES_EXE} ${SOURCE_FILES_EXE})
# If the debug configuration pass the _DEBUG define to the compiler
target_compile_definitions(${DEMO_NAME} PRIVATE $<$<CONFIG:Debug>:_DEBUG>)

# Define the include DIRs
get_target_property(CORE_LIB_INCLUDE ${CORE_LIB_NAME} INCLUDE_DIRECTORIES)

target_include_directories(${DEMO_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${DEMO_NAME} PRIVATE ${CORE_LIB_INCLUDE})

# Define the link libraries
target_link_libraries(${DEMO_NAME} ${CORE_LIB_NAME})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "sources" FILES ${SOURCE_FILES_EXE})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "headers" FILES ${HEADER_FILES_EXE})

add_custom_target(Shaders
	SOURCES
	${SHADER_FILES}
)
