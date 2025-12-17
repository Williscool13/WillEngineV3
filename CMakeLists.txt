cmake_minimum_required(VERSION 4.0)
project(will-engine)

set(CMAKE_CXX_STANDARD 23)


option(WILL_BUILD_EDITOR "Build editor features" OFF)
option(GAME_STATIC "Build game as static library" OFF)
option(WILL_BUILD_NO_CONSOLE "Disable the console" OFF)

option(PACKAGED_BUILD "Enable to use UserDataPath for log and crash report paths" OFF)

# Configuration ==============================
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=0)
elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=1)
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=2)
elseif (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=2)
endif ()

if (WILL_BUILD_EDITOR)
    add_compile_definitions(WILL_EDITOR)
endif ()

if (GAME_STATIC)
    add_compile_definitions(GAME_STATIC)
endif ()

if (PACKAGED_BUILD)
    add_compile_definitions(PACKAGED_BUILD)
else()
    add_compile_definitions(ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets")
endif ()

# Build =======================================
if (NOT WILL_BUILD_EDITOR AND WILL_BUILD_NO_CONSOLE AND WIN32)
    add_executable(will-engine WIN32 main.cpp)
else ()
    add_executable(will-engine main.cpp)
endif ()

target_include_directories(will-engine PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(will-engine PUBLIC engine)

add_dependencies(will-engine game)

# Dependencies ================================
add_compile_definitions(NOMINMAX)
add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)
add_subdirectory(extern/fmt)
set(SPDLOG_FMT_EXTERNAL ON)
add_subdirectory(extern/spdlog)
add_subdirectory(extern/SDL EXCLUDE_FROM_ALL)
add_subdirectory(extern/volk)
add_subdirectory(extern/vma)
add_subdirectory(extern/vk-bootstrap)
set(ENKITS_BUILD_EXAMPLES OFF)
add_subdirectory(extern/enkiTS)
add_subdirectory(extern/OffsetAllocator)
set(LIBKTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "" FORCE)
set(LIBKTX_FEATURE_ETC_UNPACK OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TESTS OFF)
set(KTX_FEATURE_JS OFF)
add_subdirectory(extern/KTX-Software/lib)
add_subdirectory(extern/meshoptimizer)
add_subdirectory(extern/fastgltf)

add_subdirectory(src/engine)
add_subdirectory(src/core)
add_subdirectory(src/game)
add_subdirectory(src/render)
add_subdirectory(src/utils)
add_subdirectory(src/platform)
if (WILL_BUILD_EDITOR)
    add_subdirectory(src/editor)
endif ()

# Imgui =======================================
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/extern/imgui)
add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_threaded_rendering.h
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
        ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
        src/asset-load/asset_load_thread.cpp
        src/asset-load/asset_load_thread.h
)
target_compile_definitions(imgui PUBLIC
        IMGUI_IMPL_VULKAN_USE_VOLK
        VK_NO_PROTOTYPES
)
target_include_directories(imgui PUBLIC
        ${CMAKE_SOURCE_DIR}/extern/imgui
        ${CMAKE_SOURCE_DIR}/extern/volk
        ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(imgui PUBLIC
        volk
        SDL3::SDL3
)

# Miniz =======================================
add_library(miniz STATIC
        extern/miniz/miniz.c
        extern/miniz/miniz.h
)
target_include_directories(miniz PUBLIC extern/miniz)

# Shaders =====================================
#   - Use this command to compile any shader interop that needs to be recompiled.
#     cmake --build cmake-build-debug-visual-studio --target shader_interop
#   - Or just this if you're in the build folder
#     cmake --build . --target shader_interop

preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/common_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/common_interop.slang
)

preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/model_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/model_interop.slang
)


get_property(INTEROP_FILES GLOBAL PROPERTY SHADER_INTEROP_FILES)
add_custom_target(shader_interop DEPENDS ${INTEROP_FILES})

add_custom_target(regenerate_interop
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target shader_interop
        COMMENT "Regenerating all shader interop files"
)

compile_shaders(will-engine
        ${CMAKE_SOURCE_DIR}/shaders/basicCompute.slang compute computeMain
        ${CMAKE_SOURCE_DIR}/shaders/basicRender.slang task taskMain
        ${CMAKE_SOURCE_DIR}/shaders/basicRender.slang mesh meshMain
        ${CMAKE_SOURCE_DIR}/shaders/basicRender.slang fragment fragmentMain
)


# Post-Build Commands =========================
add_custom_command(TARGET will-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:will-engine>
)