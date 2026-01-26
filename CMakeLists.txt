cmake_minimum_required(VERSION 4.0)
project(will-engine)

set(CMAKE_CXX_STANDARD 23)


option(WILL_ENGINE_BUILD_EDITOR "Build editor features" OFF)
option(WILL_ENGINE_GAME_STATIC "Build game as static library" OFF)
option(WILL_ENGINE_NO_CONSOLE "Disable the console" OFF)
option(WILL_ENGINE_PACKAGED_BUILD "Enable the use UserDataPath for log and crash report paths" OFF)
option(WILL_ENGINE_BUILD_TESTS "Build test suite" OFF)
option(WILL_ENGINE_ENABLE_PROFILER "Enables tracy profiler" OFF)
option(WILL_ENGINE_ENABLE_LOGGING "Enables logging" ON)

# Configuration ==============================
if (NOT WILL_ENGINE_ENABLE_LOGGING)
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=6)
elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
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
set(SDLMIXER_MIDI_NATIVE OFF)     # disable formats we don't use to make the build faster and smaller. Also some of these don't work on all platforms so you'll need to do some experimentation.
set(SDLMIXER_GME OFF)
set(SDLMIXER_WAVPACK OFF)
set(SDLMIXER_MOD OFF)
set(SDLMIXER_MOD_XMP OFF)
set(SDLMIXER_OPUS OFF)
set(SDLMIXER_MP3_MPG123 OFF)
set(SDLMIXER_VORBIS_VORBISFILE OFF)
set(SDLMIXER_FLAC_LIBFLAC OFF)
set(SDLMIXER_VENDORED ON)
add_subdirectory(extern/SDL_mixer EXCLUDE_FROM_ALL)
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
add_subdirectory(src/audio)
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

# bc7enc_rdo ==================================
add_library(bc7enc_rdo STATIC
        extern/bc7enc_rdo/bc7enc.cpp
        extern/bc7enc_rdo/bc7enc.h
        extern/bc7enc_rdo/bc7decomp_ref.cpp
        extern/bc7enc_rdo/bc7decomp.cpp
        extern/bc7enc_rdo/bc7decomp.h
        extern/bc7enc_rdo/rgbcx.cpp
        extern/bc7enc_rdo/rgbcx.h
        extern/bc7enc_rdo/rdo_bc_encoder.cpp
        extern/bc7enc_rdo/rdo_bc_encoder.h
        extern/bc7enc_rdo/ert.cpp
        extern/bc7enc_rdo/ert.h
        extern/bc7enc_rdo/utils.cpp
        extern/bc7enc_rdo/utils.h
        extern/bc7enc_rdo/dds_defs.h
        extern/bc7enc_rdo/lodepng.cpp
        extern/bc7enc_rdo/lodepng.h
        extern/bc7enc_rdo/miniz.h
)

target_include_directories(bc7enc_rdo PUBLIC extern/bc7enc_rdo)

# Tracy =======================================
add_library(tracy SHARED
        extern/tracy/public/TracyClient.cpp
)
target_include_directories(tracy PUBLIC
        extern/tracy/public
)
if (WILL_ENGINE_ENABLE_PROFILER)
    target_compile_definitions(tracy PUBLIC
            TRACY_ENABLE
    )
endif ()
target_compile_definitions(tracy PRIVATE TRACY_EXPORTS)

# Shaders =====================================
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
        ${CMAKE_SOURCE_DIR}/src/render/shaders/shadows_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/shadows_interop.slang
)
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/lights_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/lights_interop.slang
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

set_property(GLOBAL PROPERTY SHADER_INCLUDE_FILES "")

function(add_shader_include INCLUDE_FILE)
    get_filename_component(INCLUDE_ABS ${INCLUDE_FILE} ABSOLUTE)
    set_property(GLOBAL APPEND PROPERTY SHADER_INCLUDE_FILES ${INCLUDE_ABS})
endfunction()

add_shader_include("${CMAKE_SOURCE_DIR}/shaders/shader_constants.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/shader_functions.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/shadow_functions.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/pbr_functions.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/bindless_declarations.slang")

compile_shaders(will-engine
        ${CMAKE_SOURCE_DIR}/shaders/debug_visualize.slang compute ComputeMain debug_visualize
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputeVisibility instancing_visibility
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputeVisibilityShadows instancing_shadows_visibility
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputePrefixSum instancing_prefix_sum
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputeCompactAndGenerateIndirect instancing_indirect_construction
        ${CMAKE_SOURCE_DIR}/shaders/mesh_shading_instanced.slang task TaskMain mesh_shading_instanced
        ${CMAKE_SOURCE_DIR}/shaders/mesh_shading_instanced.slang mesh MeshMain mesh_shading_instanced
        ${CMAKE_SOURCE_DIR}/shaders/mesh_shading_instanced.slang fragment FragmentMain mesh_shading_instanced
        ${CMAKE_SOURCE_DIR}/shaders/mesh_shading_direct.slang compute ComputeMain mesh_shading_direct_build_indirect
        ${CMAKE_SOURCE_DIR}/shaders/mesh_shading_direct.slang task TaskMain mesh_shading_direct
        ${CMAKE_SOURCE_DIR}/shaders/mesh_shading_direct.slang mesh MeshMain mesh_shading_direct
        ${CMAKE_SOURCE_DIR}/shaders/mesh_shading_direct.slang fragment FragmentMain mesh_shading_direct

        ${CMAKE_SOURCE_DIR}/shaders/portal_rendering.slang fragment FragmentMain portal_composite

        ${CMAKE_SOURCE_DIR}/shaders/shadow_mesh_shading_instanced.slang task TaskMain shadow_mesh_shading_instanced
        ${CMAKE_SOURCE_DIR}/shaders/shadow_mesh_shading_instanced.slang mesh MeshMain shadow_mesh_shading_instanced
        ${CMAKE_SOURCE_DIR}/shaders/shadows_resolve.slang compute ComputeMain shadows_resolve
        ${CMAKE_SOURCE_DIR}/shaders/deferred_resolve.slang compute ComputeMain deferred_resolve
        ${CMAKE_SOURCE_DIR}/shaders/temporal_antialiasing.slang compute ComputeMain temporal_antialiasing
        ${CMAKE_SOURCE_DIR}/shaders/ground_truth_ambient_occlusion.slang compute ComputeGTAODepthPrepass gtao_depth_prepass
        ${CMAKE_SOURCE_DIR}/shaders/ground_truth_ambient_occlusion.slang compute ComputeGTAOMain gtao_main
        ${CMAKE_SOURCE_DIR}/shaders/ground_truth_ambient_occlusion.slang compute ComputeGTAODenoise gtao_denoise
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeSDRTonemap tonemap_sdr
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeBuildHistogramExposure exposure_build_histogram
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeAverageExposure exposure_calculate_average
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeMotionBlurTileMax motion_blur_tile_max
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeMotionBlurNeighborMax motion_blur_neighbor_max
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeMotionBlurReconstruction motion_blur_reconstruction
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeBloomThreshold bloom_threshold
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeBloomDownsample bloom_downsample
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeBloomUpsample bloom_upsample
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeVignetteAberration vignette_aberration
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeFilmGrain film_grain
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeSharpening sharpening
        ${CMAKE_SOURCE_DIR}/shaders/post_process.slang compute ComputeColorGrading color_grading

        ${CMAKE_SOURCE_DIR}/shaders/common_shaders.slang vertex FullscreenPassVertexMain fullscreen_pass
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