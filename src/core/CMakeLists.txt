# Copyright (C) 2022 Tomasz Gałaj

set(CORE_LIB_NAME "RapidGLCore" CACHE STRING "Name of the core library")

# Add source files
file(GLOB_RECURSE SOURCE_FILES_CORE 
	 ${CMAKE_CURRENT_SOURCE_DIR}/*.c
	 ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
	 ${CMAKE_CURRENT_SOURCE_DIR}/gui/*.c
	 ${CMAKE_CURRENT_SOURCE_DIR}/gui/*.cpp)
	
# Add header files
file(GLOB_RECURSE HEADER_FILES_CORE
     ${CMAKE_CURRENT_SOURCE_DIR}/*.h
	 ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
	 ${CMAKE_CURRENT_SOURCE_DIR}/gui/*.h
	 ${CMAKE_CURRENT_SOURCE_DIR}/gui/*.hpp)

# Define the library
add_library(${CORE_LIB_NAME} STATIC ${HEADER_FILES_CORE} ${SOURCE_FILES_CORE})

target_compile_definitions(${CORE_LIB_NAME} PRIVATE GLFW_INCLUDE_NONE)
target_compile_definitions(${CORE_LIB_NAME} PRIVATE LIBRARY_SUFFIX="")

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
