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
	camera.cpp
	core_app.cpp
	filesystem.cpp
	frustum.cpp
	GLTimer.cpp
	input.cpp
	input_bind.cpp
	material.cpp
	plane.cpp
	postprocess.cpp
	postprocess_chain.cpp
	pp_bloom.cpp
	pp_gaussian_blur.cpp
	pp_gaussian_blur_fixed.cpp
	pp_light_scattering.cpp
	pp_mipmap_blur.cpp
	pp_tonemapping.cpp
	rendertarget_2d.cpp
	rendertarget_common.cpp
	rendertarget_cube.cpp
	shader.cpp
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
	input.h
	input_bind.h
	material.h
	mesh_part.h
	plane.h
	postprocess.h
	postprocess_chain.h
	pp_bloom.h
	pp_gaussian_blur.h
	pp_gaussian_blur_fixed.h
	pp_light_scattering.h
	pp_mipmap_blur.h
	pp_tonemapping.h
	rendertarget_2d.h
	rendertarget_common.h
	rendertarget_cube.h
	ringbuffer.h
	sample_window.h
	shader.h
	spatial_allocator.h
	ssbo.h
	static_model.h
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
message(DEBUG ${GLAD_HEADER})

set(GLAD_LOOKUP_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/glad_lookup_to_cpp)
set(GLAD_LOOKUP_INPUT  ${GLAD_HEADER})
set(GLAD_LOOKUP_OUTPUT ${CMAKE_CURRENT_LIST_DIR}/generated/glad_lookup.cpp)

# Make sure output directory exists
file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)

add_custom_command(
	OUTPUT ${GLAD_LOOKUP_OUTPUT}
	COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=$ENV{PYTHONPATH}
	        ${Python3_EXECUTABLE} ${GLAD_LOOKUP_SCRIPT} < ${GLAD_LOOKUP_INPUT} > ${GLAD_LOOKUP_OUTPUT}
			DEPENDS ${GLAD_LOOKUP_SCRIPT} ${GAD_LOOKUP_INPUT} ${GLAD_HEADER}
	COMMENT "Generating C++ lookup from glad.h"
	VERBATIM
)

add_custom_target(generate_glad_lookup DEPENDS ${GLAD_LOOKUP_OUTPUT})
# Make sure the build depends on this output
add_dependencies(${CORE_LIB_NAME} generate_glad_lookup)

target_sources(${CORE_LIB_NAME} PRIVATE ${GLAD_LOOKUP_OUTPUT})
