# Copyright (C) 2022 Tomasz Gałaj

cmake_minimum_required(VERSION 3.21 FATAL_ERROR)
project(RapidGL VERSION 1.0)

set(OpenGL_GL_PREFERENCE GLVND)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(global_settings)

# ---- Configure the root_directory file ----
configure_file(${CMAKE_SOURCE_DIR}/configuration/root_directory.h.in ${CMAKE_SOURCE_DIR}/configuration/root_directory.h)

# ---- Dependencies ----
add_subdirectory(thirdparty)

# ---- Main project's files ----
add_subdirectory(src)


file(GLOB_RECURSE SCRIPT_FILES ${CMAKE_CURRENT_LIST_DIR}/scripts/*)

add_custom_target(Scripts
	SOURCES
	${SCRIPT_FILES}
)

set(SHADER_PATH resources/shaders)
set(SHADER_FILES
	${SHADER_PATH}/background.frag
	${SHADER_PATH}/background.vert
	${SHADER_PATH}/billboard-icon.vert
	${SHADER_PATH}/billboard-icon.frag
	${SHADER_PATH}/blur_3d.comp
	${SHADER_PATH}/collect_nonempty_clusters.comp
	${SHADER_PATH}/cull_lights.comp
	${SHADER_PATH}/depth_pass.frag
	${SHADER_PATH}/depth_pass.vert
	${SHADER_PATH}/downscale.comp
	${SHADER_PATH}/draw2d_line.frag
	${SHADER_PATH}/draw2d_rectangle.frag
	${SHADER_PATH}/find_nonempty_clusters.comp
	${SHADER_PATH}/frustum.glh
	${SHADER_PATH}/FSQ.frag
	${SHADER_PATH}/FSQ.vert
	${SHADER_PATH}/gaussian_blur.comp
	${SHADER_PATH}/generate_clusters.comp
	${SHADER_PATH}/imgui_3d_texture.frag
	${SHADER_PATH}/imgui_3d_texture.vert
	${SHADER_PATH}/imgui_depth_image.frag
	${SHADER_PATH}/imgui_depth_image.vert
	${SHADER_PATH}/line_draw.frag
	${SHADER_PATH}/line_draw.vert
	${SHADER_PATH}/mipmap_blur.comp
	${SHADER_PATH}/noise.glh
	${SHADER_PATH}/pbr_clustered.frag
	${SHADER_PATH}/pbr_lighting.glh
	${SHADER_PATH}/pbr_lighting.vert
	${SHADER_PATH}/rect_light_ltc.glh
	${SHADER_PATH}/seven_segment_number.frag
	${SHADER_PATH}/shadow_depth.frag
	${SHADER_PATH}/shadow_depth.vert
	${SHADER_PATH}/shadows.glh
	${SHADER_PATH}/shapes.glh
	${SHADER_PATH}/shared-structs.glh
	${SHADER_PATH}/surface_light_geom.frag
	${SHADER_PATH}/surface_light_geom.vert
	${SHADER_PATH}/tmo.frag
	${SHADER_PATH}/upscale.comp
	${SHADER_PATH}/volumetrics.glh
	${SHADER_PATH}/volumetrics_accumulate.comp
	${SHADER_PATH}/volumetrics_bake.comp
	${SHADER_PATH}/volumetrics_cull.comp
	${SHADER_PATH}/volumetrics_inject.comp
	${SHADER_PATH}/volumetrics_select_lights.comp
)

add_custom_target(CoreShaders
	SOURCES
	${SHADER_FILES}
)
