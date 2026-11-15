# CPM - package manager
include(CPM)

# OpenGL
find_package(OpenGL REQUIRED)

# glad - local copy
set(GLAD_DIR ${CMAKE_CURRENT_LIST_DIR}/glad)
set(glad_SOURCE_DIR ${GLAD_DIR}/include CACHE INTERNAL "")
add_library(glad STATIC ${GLAD_DIR}/src/glad.c)
target_include_directories(glad PRIVATE ${GLAD_DIR}/include)

# stb_image - local copy
set(STB_IMAGE_DIR ${CMAKE_CURRENT_LIST_DIR}/stb_image)
set(stb_image_SOURCE_DIR ${STB_IMAGE_DIR} CACHE INTERNAL "")
add_library(stb_image STATIC ${STB_IMAGE_DIR}/stb_image.cpp 
						     ${STB_IMAGE_DIR}/stb_image_write.cpp 
                             ${STB_IMAGE_DIR}/stb_image_resize2.cpp)
target_include_directories(stb_image PRIVATE ${STB_IMAGE_DIR})

# other - external
CPMAddPackage("gh:assimp/assimp@6.0.5")
CPMAddPackage("gh:glfw/glfw#3.5.1")
CPMAddPackage("gh:g-truc/glm#1.0.3")
#CPMAddPackage("gh:gabime/spdlog@1.10.0")
CPMAddPackage("gh:martinus/unordered_dense@4.11.0")
CPMAddPackage("gh:martinus/svector@1.4.0")
#CPMAddPackage("gh:libjxl/libjxl@0.7.2")   TODO fails with "set_target_properties can not be used on an ALIAS target"
CPMAddPackage(
    NAME signals
    GITHUB_REPOSITORY TheWisp/signals
    GIT_TAG 7ff59d1bc6387337ef34856a450be34cfa2c9e51
    DOWNLOAD_ONLY TRUE   # its own CMakeLists only builds a benchmark executable
)
if(signals_ADDED)
    # header is flat in the repo root; stage it so it's included as <TheWisp/signals.hpp>
    set(signals_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/signals-include)
    configure_file(${signals_SOURCE_DIR}/signals.hpp ${signals_INCLUDE_DIR}/TheWisp/signals.hpp COPYONLY)
    add_library(signals INTERFACE)
    target_include_directories(signals INTERFACE ${signals_INCLUDE_DIR})
endif()


CPMAddPackage(
    NAME boost-ut
    GITHUB_REPOSITORY boost-ext/ut
    GIT_TAG v2.3.1
    OPTIONS "BOOST_UT_ENABLE_ASSERT=OFF"
)
# seems source dir isn't propagated? setting it manually
set(boost-ut_SOURCE_DIR "${boost-ut_SOURCE_DIR}" CACHE INTERNAL "")

CPMAddPackage(
    GITHUB_REPOSITORY benikabocha/tinyddsloader
    GIT_TAG master
    GIT_SHALLOW TRUE
    DOWNLOAD_ONLY TRUE
)
if (tinyddsloader_ADDED)
    add_library(tinyddsloader INTERFACE)
    target_include_directories(tinyddsloader INTERFACE "${tinyddsloader_SOURCE_DIR}")
endif()

CPMAddPackage("gh:skypjack/entt@3.16.0")

CPMAddPackage("gh:ocornut/imgui@1.92.3-docking")
if(imgui_ADDED)
    set(imgui_SOURCE_DIR ${imgui_SOURCE_DIR} CACHE INTERNAL "")
    add_library(imgui
        STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
endif()

CPMAddPackage("gh:CedricGuillemet/ImGuizmo#a15acd87a3f3241a29ea1363ceafc680dca3a96b")
if(ImGuizmo_ADDED)
    set(ImGuizmo_SOURCE_DIR ${ImGuizmo_SOURCE_DIR} CACHE INTERNAL "")
    add_library(imguizmo
        STATIC
        ${ImGuizmo_SOURCE_DIR}/GraphEditor.cpp
        ${ImGuizmo_SOURCE_DIR}/ImCurveEdit.cpp
        ${ImGuizmo_SOURCE_DIR}/ImGradient.cpp
        ${ImGuizmo_SOURCE_DIR}/ImGuizmo.cpp
        # ${ImGuizmo_SOURCE_DIR}/ImSequencer.cpp
    )
    target_include_directories(imguizmo PUBLIC ${ImGuizmo_SOURCE_DIR})
    target_link_libraries(imguizmo PUBLIC imgui)
endif()

# CPMAddPackage(
#   NAME implot
#   GITHUB_REPOSITORY epezent/implot
#   GIT_TAG v0.16
#   DOWNLOAD_ONLY TRUE
# )
# if(implot_ADDED)
#   add_library(implot
#     STATIC
#     ${implot_SOURCE_DIR}/implot.cpp
#     ${implot_SOURCE_DIR}/implot_items.cpp)
#   target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})

#   # ImPlot needs ImGui, so link that too
#   target_link_libraries(implot PUBLIC imgui)
# endif()

set_target_properties(glad
                      stb_image 
                      assimp 
                      glfw 
                      glm 
                      imgui 
                      # implot
                      imguizmo
					  unordered_dense
					  svector
					  tinyddsloader
					  signals
					  PROPERTIES FOLDER "thirdparty")

if (TARGET zlibstatic)
    set_target_properties(zlibstatic PROPERTIES FOLDER "thirdparty")
endif()
