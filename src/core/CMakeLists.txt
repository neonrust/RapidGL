# Copyright (C) 2022 Tomasz Gałaj

set(CORE_LIB_NAME "RapidGLCore" CACHE STRING "Name of the core library")

# Add source files
set(SOURCE_FILES_CORE
	gui/font.cpp
	gui/gui.cpp
	gui/imgui_impl_glfw.cpp
	gui/imgui_impl_opengl3.cpp
	animated_model.cpp
	bounds.cpp
	buffer.cpp
	buffer_binds.h
	camera.cpp
	core_app.cpp
	filesystem.cpp
	frustum.cpp
	GLTimer.cpp
	input.cpp
	input_bind.cpp
	instance_attributes.cpp
	ktx_loader.cpp
	light_manager.cpp
	log.cpp
	material.cpp
	plane.cpp
	postprocess.cpp
	postprocess_chain.cpp
	pp_bloom.cpp
	pp_gaussian_blur.cpp
	pp_gaussian_blur_fixed.cpp
	pp_volumetrics.cpp
	pp_mipmap_blur.cpp
	pp_tonemapping.cpp
	rendertarget_2d.cpp
	rendertarget_common.cpp
	rendertarget_cube.cpp
	shader.cpp
	shadow_atlas.cpp
	static_model.cpp
	texture.cpp
	util.cpp
	window.cpp
	zstr.cpp
)

# Add header files
set(HEADER_FILES_CORE
	gui/font.h
	gui/gui.h
	gui/imgui_impl_glfw.h
	gui/imgui_impl_opengl3.h
	gui/imgui_impl_opengl3_loader.h
	animated_model.h
	bounds.h
	buffer.h
	camera.h
	common.h
	container_types.h
	core_app.h
	debug_output_gl.h
	filesystem.h
	frustum.h
	gl_lookup.h
	GLTimer.h
	hash_vec3.h
	hash_combine.h
	hash_mat4.h
	hash_vec4.h
	input.h
	input_bind.h
	instance_attributes.h
	ktx_loader.h
	light_constants.h
	light_manager.h
	lights.h
	log.h
	material.h
	mesh_part.h
	plane.h
	postprocess.h
	postprocess_chain.h
	pp_bloom.h
	pp_gaussian_blur.h
	pp_gaussian_blur_fixed.h
	pp_volumetrics.h
	pp_mipmap_blur.h
	pp_tonemapping.h
	rendertarget_2d.h
	rendertarget_common.h
	rendertarget_cube.h
	ringbuffer.h
	sample_window.h
	shader.h
	shadow_atlas.h
	spatial_allocator.h
	ssbo.h
	static_model.h
	static_object.h
	texture.h
	timer.h
	ubo.h
	util.h
	window.h
	zstr.h
	stack_container.h
	constants.h
)


# Define the library
add_library(${CORE_LIB_NAME} STATIC ${HEADER_FILES_CORE} ${SOURCE_FILES_CORE})

add_subdirectory(tests)

target_compile_definitions(${CORE_LIB_NAME} PRIVATE GLFW_INCLUDE_NONE)
target_compile_definitions(${CORE_LIB_NAME} PRIVATE LIBRARY_SUFFIX="")
# If the debug configuration pass the DEBUG define to the compiler
target_compile_definitions(${CORE_LIB_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG>)

target_include_directories(${CORE_LIB_NAME}
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/thirdparty
	${CMAKE_SOURCE_DIR}/configuration
	${glad_SOURCE_DIR}
	${stb_image_SOURCE_DIR}
	${imgui_SOURCE_DIR}
	${unordered_dense_SOURCE_DIR}/include
	${svector_SOURCE_DIR}/include
)

target_link_libraries(${CORE_LIB_NAME}
	${OPENGL_LIBRARIES}
	glad
	stb_image
	jxl
	jxl_threads
	ktx
	assimp
	glfw
	imgui
	glm::glm
	tinyddsloader
)

# target_compile_options(${CORE_LIB_NAME}
# 	PRIVATE
# 	-Wextra
# 	-Wall
# 	-Wpedantic
# 	-Werror
# 	--pedantic-errors
# 	-Wconversion
# 	-Wmissing-declarations
# 	-Wold-style-cast
# 	-Wno-padded
# )

set_source_files_properties(
	gui/imgui_impl_glfw.cpp
	gui/imgui_impl_glfw.h
	gui/imgui_impl_opengl3.cpp
	gui/imgui_impl_opengl3.h
	gui/imgui_impl_opengl3_loader.h
	PROPERTIES
	COMPILE_FLAGS " -Wno-old-style-cast -Wno-conversion "
)
if(MinGW)
    target_link_libraries(${CORE_LIB_NAME} bz2)
endif()

if(MSVC)
    target_compile_definitions(${CORE_LIB_NAME} PUBLIC NOMINMAX)
endif()



# Read the file contents
set(GLAD_HEADER "${glad_SOURCE_DIR}/glad/glad.h")

set(GL_LOOKUP_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/gl_lookup_to_cpp)
set(GL_LOOKUP_INPUT  ${GLAD_HEADER})
set(GL_LOOKUP_OUTPUT ${CMAKE_CURRENT_LIST_DIR}/generated/gl_lookup.cpp)

# Make sure output directory exists
file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)

add_custom_command(
	OUTPUT ${GL_LOOKUP_OUTPUT}
	COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=$ENV{PYTHONPATH}
	        ${Python3_EXECUTABLE} ${GL_LOOKUP_SCRIPT} < ${GL_LOOKUP_INPUT} > ${GL_LOOKUP_OUTPUT}
			DEPENDS ${GL_LOOKUP_SCRIPT} ${GAD_LOOKUP_INPUT} ${GLAD_HEADER}
	COMMENT "Generating C++ lookup from glad.h -> ${GL_LOOKUP_OUTPUT}"
	VERBATIM
)

add_custom_target(generate_GL_LOOKUP DEPENDS ${GL_LOOKUP_OUTPUT})
# Make sure the build depends on this output
add_dependencies(${CORE_LIB_NAME} generate_GL_LOOKUP)

# Read the file contents
# set(LIMITS_HEADER "${CMAKE_CURRENT_LIST_DIR}/light_constants.h")
# file(READ "${LIMITS_HEADER}" LIMITS_HEADER_CONTENTS)

# Helper macro to extract #define value
# macro(extract_define NAME)
# 	set(_pattern "#define[ \t]+${NAME}[ \t]+([0-9]+)")
# 	string(REGEX MATCH ${_pattern} _match "${LIMITS_HEADER_CONTENTS}")
# 	if(NOT ${CMAKE_MATCH_1})
# 		    message(FATAL_ERROR "Constant ${NAME} not found")
# 	endif()
# 	set(${NAME} "${CMAKE_MATCH_1}" CACHE INTERNAL "Extracted constant")
# 	message(STATUS "SET ${NAME} = ${CMAKE_MATCH_1}")
# endmacro()

# Extract constants (repeat for each)
# extract_define(MAX_POINT_LIGHTS)
# extract_define(MAX_SPOT_LIGHTS)
# extract_define(MAX_RECT_LIGHTS)
# extract_define(MAX_TUBE_LIGHTS)
# extract_define(MAX_SPHERE_LIGHTS)
# extract_define(MAX_DISC_LIGHTS)
# extract_define(CLUSTER_MAX_COUNT)
# extract_define(CLUSTER_MAX_LIGHTS)


set(GLSL_STRUCT_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/glsl_struct_to_cpp)
set(GLSL_STRUCT_INPUT  ${CMAKE_SOURCE_DIR}/resources/shaders/shared-structs.glh)
set(GLSL_STRUCT_OUTPUT ${CMAKE_CURRENT_LIST_DIR}/generated/shared-structs.h)

add_custom_command(
	OUTPUT ${GLSL_STRUCT_OUTPUT}
	COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=$ENV{PYTHONPATH}
	        ${Python3_EXECUTABLE} ${GLSL_STRUCT_SCRIPT} ${GLSL_STRUCT_INPUT}
			# -DMAX_POINT_LIGHTS=${MAX_POINT_LIGHTS}
			# -DMAX_SPOT_LIGHTS=${MAX_SPOT_LIGHTS}
			# -DMAX_RECT_LIGHTS=${MAX_RECT_LIGHTS}
			# -DMAX_TUBE_LIGHTS=${MAX_TUBE_LIGHTS}
			# -DMAX_SPHERE_LIGHTS=${MAX_SPHERE_LIGHTS}
			# -DMAX_DISC_LIGHTS=${MAX_DISC_LIGHTS}
			# -DCLUSTER_MAX_COUNT=${CLUSTER_MAX_COUNT}
			# -DCLUSTER_MAX_LIGHTS=${CLUSTER_MAX_LIGHTS}
			> ${GLSL_STRUCT_OUTPUT}
			DEPENDS ${GLSL_STRUCT_SCRIPT} ${GLSL_STRUCT_INPUT} ${LIMITS_HEADER}
	COMMENT "Generating C++ structs from GLSL"
	VERBATIM
)

# copy some header files also used by shaders#
configure_file(${CMAKE_CURRENT_LIST_DIR}/light_constants.h ${CMAKE_SOURCE_DIR}/resources/shaders/ COPYONLY)
configure_file(${CMAKE_CURRENT_LIST_DIR}/buffer_binds.h ${CMAKE_SOURCE_DIR}/resources/shaders/ COPYONLY)

add_custom_target(generate_glsl_structs DEPENDS ${GLSL_STRUCT_OUTPUT})

# Make sure the main build depends on this output
add_dependencies(${CORE_LIB_NAME} generate_glsl_structs)
# Or, if you add the generated directory to your include paths:
#target_include_directories(${CORE_LIB_NAME} PRIVATE ${CMAKE_BINARY_DIR}/generated)

target_sources(${CORE_LIB_NAME} PRIVATE ${GL_LOOKUP_OUTPUT})
