# Copyright (C) 2022 Tomasz Gałaj

set(DEMO_NAME "27_clustered_shading")

# Add source files
file(GLOB_RECURSE SOURCE_FILES_EXE 
	 ${CMAKE_CURRENT_SOURCE_DIR}/*.c
	 ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# Add header files
file(GLOB_RECURSE HEADER_FILES_EXE 
	 ${CMAKE_CURRENT_SOURCE_DIR}/*.h
	 ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

file(GLOB_RECURSE SHADER_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/*.frag
	${CMAKE_CURRENT_SOURCE_DIR}/*.vert
	${CMAKE_CURRENT_SOURCE_DIR}/*.comp
	${CMAKE_CURRENT_SOURCE_DIR}/*.glh
)
# Define the executable
add_executable(${DEMO_NAME} ${HEADER_FILES_EXE} ${SOURCE_FILES_EXE})

# Define the include DIRs
get_target_property(CORE_LIB_INCLUDE ${CORE_LIB_NAME} INCLUDE_DIRECTORIES)

target_include_directories(${DEMO_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${DEMO_NAME} PRIVATE ${CORE_LIB_INCLUDE})

# Define the link libraries
target_link_libraries(${DEMO_NAME} ${CORE_LIB_NAME})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "sources" FILES ${SOURCE_FILES_EXE})						   
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "headers" FILES ${HEADER_FILES_EXE})



# Read the file contents
set(LIMITS_HEADER "${CMAKE_CURRENT_LIST_DIR}/light_constants.h")
file(READ "${LIMITS_HEADER}" LIMITS_HEADER_CONTENTS)
#message(STATUS "Header contents: [${LIMITS_HEADER_CONTENTS}]")

# Helper macro to extract #define value
macro(extract_define NAME)
	set(_pattern "#define[ \t]+${NAME}[ \t]+([0-9]+)")
	string(REGEX MATCH ${_pattern} _match "${LIMITS_HEADER_CONTENTS}")
	if(NOT ${CMAKE_MATCH_1})
		    message(FATAL_ERROR "Constant ${NAME} not found")
	endif()
	set(${NAME} "${CMAKE_MATCH_1}" CACHE INTERNAL "Extracted constant")
endmacro()

# Extract constants (repeat for each)
extract_define(MAX_POINT_LIGHTS)
extract_define(MAX_SPOT_LIGHTS)
extract_define(MAX_AREA_LIGHTS)
extract_define(CLUSTER_MAX_COUNT)
extract_define(CLUSTER_MAX_LIGHTS)

set(GLSL_STRUCT_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/glsl_struct_to_cpp)
set(GLSL_STRUCT_INPUT  ${CMAKE_CURRENT_LIST_DIR}/shaders/shared-structs.glh)
set(GLSL_STRUCT_OUTPUT ${CMAKE_CURRENT_LIST_DIR}/generated/shared-structs.h)

# Make sure output directory exists
file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)

add_custom_command(
	OUTPUT ${GLSL_STRUCT_OUTPUT}
	COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=$ENV{PYTHONPATH}
	        ${Python3_EXECUTABLE} ${GLSL_STRUCT_SCRIPT} ${GLSL_STRUCT_INPUT}
			-DMAX_POINT_LIGHTS=${MAX_POINT_LIGHTS}
			-DMAX_SPOT_LIGHTS=${MAX_SPOT_LIGHTS}
			-DMAX_AREA_LIGHTS=${MAX_AREA_LIGHTS}
			-DCLUSTER_MAX_COUNT=${CLUSTER_MAX_COUNT}
			-DCLUSTER_MAX_LIGHTS=${CLUSTER_MAX_LIGHTS}
			> ${GLSL_STRUCT_OUTPUT}
			DEPENDS ${GLSL_STRUCT_SCRIPT} ${GLSL_STRUCT_INPUT} ${LIMITS_HEADER}
	COMMENT "Generating C++ structs from GLSL"
	VERBATIM
)

add_custom_target(generate_glsl_structs DEPENDS ${GLSL_STRUCT_OUTPUT})
# Make sure your main build depends on this output
add_dependencies(${DEMO_NAME} generate_glsl_structs)
# Or, if you add the generated directory to your include paths:
#target_include_directories(${DEMO_NAME} PRIVATE ${CMAKE_BINARY_DIR}/generated)


add_custom_target(Other-files
	SOURCES
	${SHADER_FILES}
)
