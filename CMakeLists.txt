cmake_minimum_required(VERSION 4.0)
project(will-engine)

set(CMAKE_CXX_STANDARD 23)


option(WILL_ENGINE_BUILD_EDITOR "Build editor features" OFF)
option(WILL_ENGINE_GAME_STATIC "Build game as static library" OFF)
option(WILL_ENGINE_NO_CONSOLE "Disable the console" OFF)
option(WILL_ENGINE_PACKAGED_BUILD "Enable the use UserDataPath for log and crash report paths" OFF)
option(WILL_ENGINE_BUILD_TESTS "Build test suite" OFF)
option(WILL_ENGINE_ENABLE_PROFILER "Enables tracy profiler" ON)

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

if (WILL_ENGINE_BUILD_EDITOR)
    add_compile_definitions(WILL_EDITOR)
endif ()

if (WILL_ENGINE_GAME_STATIC)
    add_compile_definitions(GAME_STATIC)
endif ()

if (WILL_ENGINE_PACKAGED_BUILD)
    add_compile_definitions(PACKAGED_BUILD)
else ()
    add_compile_definitions(ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets")
endif ()

# Build =======================================
if (NOT WILL_ENGINE_BUILD_EDITOR AND WILL_ENGINE_NO_CONSOLE AND WIN32)
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
add_subdirectory(src/physics)
add_subdirectory(src/asset-load)
add_subdirectory(src/utils)
add_subdirectory(src/platform)

if (WILL_ENGINE_BUILD_EDITOR)
    add_subdirectory(src/editor)
endif ()

if (WILL_ENGINE_BUILD_TESTS)
    add_subdirectory(extern/Catch2)
    add_subdirectory(tests)
endif ()

# Jolt ========================================
set(PHYSICS_REPO_ROOT "${CMAKE_SOURCE_DIR}/extern/JoltPhysics")
include("${PHYSICS_REPO_ROOT}/Jolt/Jolt.cmake")
target_include_directories(Jolt PUBLIC
        ${PHYSICS_REPO_ROOT}
)
set(CMAKE_CXX_STANDARD 23) # Jolt sets cxx standard to 17

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

# Tracy =======================================
add_library(tracy SHARED
        extern/tracy/public/TracyClient.cpp
)
target_include_directories(tracy PUBLIC
        extern/tracy/public
)
if(WILL_ENGINE_ENABLE_PROFILER)
    target_compile_definitions(tracy PUBLIC
            TRACY_ENABLE
    )
endif()
target_compile_definitions(tracy PRIVATE TRACY_EXPORTS)
# TinyGLTF ====================================
add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE
        ${CMAKE_SOURCE_DIR}/extern/tinygltf
        ${CMAKE_SOURCE_DIR}/extern/json/nlohmann
)
target_compile_definitions(tinygltf INTERFACE
        TINYGLTF_NO_STB_IMAGE
        TINYGLTF_NO_STB_IMAGE_WRITE
)

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
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/instancing_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/instancing_interop.slang
)
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/constants_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/constants_interop.slang
)
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/push_constant_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/push_constant_interop.slang
)


get_property(INTEROP_FILES GLOBAL PROPERTY SHADER_INTEROP_FILES)
add_custom_target(shader_interop DEPENDS ${INTEROP_FILES})

add_custom_target(regenerate_interop
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target shader_interop
        COMMENT "Regenerating all shader interop files"
)

set_property(GLOBAL PROPERTY SHADER_INCLUDE_FILES "")

function(add_shader_include INCLUDE_FILE)
    get_filename_component(INCLUDE_ABS ${INCLUDE_FILE} ABSOLUTE)
    set_property(GLOBAL APPEND PROPERTY SHADER_INCLUDE_FILES ${INCLUDE_ABS})
endfunction()

add_shader_include("${CMAKE_SOURCE_DIR}/shaders/shader_functions.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/pbr_functions.slang")

compile_shaders(will-engine
        ${CMAKE_SOURCE_DIR}/shaders/debugVisualize.slang compute computeMain
        ${CMAKE_SOURCE_DIR}/shaders/basicCompute.slang compute computeMain
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute computeVisibility instancingVisibility
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute computePrefixSum instancingPrefixSum
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute computeCompactAndGenerateIndirect instancingCompactAndGenerateIndirect
        ${CMAKE_SOURCE_DIR}/shaders/basicRender.slang task taskMain
        ${CMAKE_SOURCE_DIR}/shaders/basicRender.slang mesh meshMain
        ${CMAKE_SOURCE_DIR}/shaders/basicRender.slang fragment fragmentMain
        ${CMAKE_SOURCE_DIR}/shaders/meshShading.slang task taskMain
        ${CMAKE_SOURCE_DIR}/shaders/meshShading.slang mesh meshMain
        ${CMAKE_SOURCE_DIR}/shaders/meshShading.slang fragment fragmentMain
        ${CMAKE_SOURCE_DIR}/shaders/meshShadingInstanced.slang task taskMain
        ${CMAKE_SOURCE_DIR}/shaders/meshShadingInstanced.slang mesh meshMain
        ${CMAKE_SOURCE_DIR}/shaders/meshShadingInstanced.slang fragment fragmentMain
        ${CMAKE_SOURCE_DIR}/shaders/deferredResolve.slang compute computeMain
        ${CMAKE_SOURCE_DIR}/shaders/temporalAntialiasing.slang compute computeMain
)

# Post-Build Commands =========================
add_custom_command(TARGET will-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:will-engine>
)
add_custom_command(TARGET will-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:tracy>
        $<TARGET_FILE_DIR:will-engine>
)