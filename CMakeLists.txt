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
option(WILL_ENGINE_ENABLE_VULKAN_VALIDATION "Enables Vulkan validation error" OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(WILL_ENGINE_DEBUG "Enable WDEBUG developer code paths" ON)
else()
    option(WILL_ENGINE_DEBUG "Enable WDEBUG developer code paths" OFF)
endif()

# Configuration ==============================
if (NOT WILL_ENGINE_ENABLE_LOGGING)
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=6)
elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=0)
    add_compile_definitions(LOGGING_ENABLED)
elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=1)
    add_compile_definitions(LOGGING_ENABLED)
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=2)
    add_compile_definitions(LOGGING_ENABLED)
elseif (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=2)
    add_compile_definitions(LOGGING_ENABLED)
endif ()

if (WILL_ENGINE_BUILD_EDITOR)
    add_compile_definitions(WILL_EDITOR)
endif ()

if (WILL_ENGINE_GAME_STATIC)
    add_compile_definitions(GAME_STATIC)
endif ()

if (WILL_ENGINE_ENABLE_VALIDATION)
    add_compile_definitions(ENABLE_VULKAN_VALIDATION)
endif ()

if (WILL_ENGINE_PACKAGED_BUILD)
    # todo remove when actually making packaged builds
    add_compile_definitions(ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets")
    add_compile_definitions(CONFIG_PATH="${CMAKE_SOURCE_DIR}/config")
    add_compile_definitions(PACKAGED_BUILD)
else ()
    add_compile_definitions(ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets")
    add_compile_definitions(CONFIG_PATH="${CMAKE_SOURCE_DIR}/config")
endif ()

if (WILL_ENGINE_ENABLE_PROFILER)
    add_compile_definitions(PROFILER_ENABLED)
endif ()

if (WILL_ENGINE_DEBUG)
    add_compile_definitions(WDEBUG)
    add_compile_definitions(JPH_DEBUG_RENDERER)
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
# For SDL and SDL_mixer
set(BUILD_SHARED_LIBS ON CACHE BOOL "Build Shared Libraries" FORCE)
add_subdirectory(extern/SDL EXCLUDE_FROM_ALL)
set(SDLMIXER_MIDI_NATIVE OFF)
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
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build Shared Libraries" FORCE)

add_compile_definitions(NOMINMAX)
add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)
# EnTT Debug-build perf on Windows: https://github.com/skypjack/entt/wiki/Frequently-Asked-Questions#why-is-my-debug-build-on-windows-so-slow
add_compile_definitions(ENTT_DISABLE_ASSERT)
if (MSVC)
    add_compile_definitions($<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
    add_compile_options($<$<CONFIG:Debug>:/JMC->)
endif ()
add_subdirectory(extern/fmt)
set(SPDLOG_FMT_EXTERNAL ON)
add_subdirectory(extern/spdlog)
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
set(FASTGLTF_ENABLE_DEPRECATED_EXT ON CACHE BOOL "" FORCE)
add_subdirectory(extern/fastgltf)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
add_subdirectory(extern/freetype)
add_library(Freetype::Freetype ALIAS freetype)
set(MSDF_ATLAS_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_VCPKG OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_SKIA OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_NO_ARTERY_FONT ON CACHE BOOL "" FORCE)
set(MSDF_ATLAS_DYNAMIC_RUNTIME ON CACHE BOOL "" FORCE)
set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_VCPKG OFF CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_SVG ON CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_PNG ON CACHE BOOL "" FORCE)
add_subdirectory(extern/msdf-atlas-gen)
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

# ImGuizmo =======================================
set(IMGUIZMO_DIR ${CMAKE_SOURCE_DIR}/extern/imguizmo)
add_library(imguizmo STATIC
        ${IMGUIZMO_DIR}/ImGuizmo.cpp
)
target_include_directories(imguizmo PUBLIC
        ${IMGUIZMO_DIR}
)
target_link_libraries(imguizmo PUBLIC
        imgui
)

# Miniz =======================================
add_library(miniz STATIC
        extern/miniz/miniz.c
        extern/miniz/miniz.h
)
target_include_directories(miniz PUBLIC extern/miniz)

# lz4 =========================================
add_library(lz4 STATIC
        extern/lz4/lz4.c
        extern/lz4/lz4.h
        extern/lz4/lz4hc.c
        extern/lz4/lz4hc.h
)
target_include_directories(lz4 PUBLIC extern/lz4)

# zstd ========================================
add_library(zstd STATIC
        extern/zstd/lib/common/debug.c
        extern/zstd/lib/common/entropy_common.c
        extern/zstd/lib/common/error_private.c
        extern/zstd/lib/common/fse_decompress.c
        extern/zstd/lib/common/pool.c
        extern/zstd/lib/common/threading.c
        extern/zstd/lib/common/xxhash.c
        extern/zstd/lib/common/zstd_common.c
        extern/zstd/lib/compress/fse_compress.c
        extern/zstd/lib/compress/hist.c
        extern/zstd/lib/compress/huf_compress.c
        extern/zstd/lib/compress/zstd_compress.c
        extern/zstd/lib/compress/zstd_compress_literals.c
        extern/zstd/lib/compress/zstd_compress_sequences.c
        extern/zstd/lib/compress/zstd_compress_superblock.c
        extern/zstd/lib/compress/zstd_double_fast.c
        extern/zstd/lib/compress/zstd_fast.c
        extern/zstd/lib/compress/zstd_lazy.c
        extern/zstd/lib/compress/zstd_ldm.c
        extern/zstd/lib/compress/zstd_opt.c
        extern/zstd/lib/compress/zstd_preSplit.c
        extern/zstd/lib/compress/zstdmt_compress.c
        extern/zstd/lib/decompress/huf_decompress.c
        extern/zstd/lib/decompress/zstd_ddict.c
        extern/zstd/lib/decompress/zstd_decompress.c
        extern/zstd/lib/decompress/zstd_decompress_block.c
)
target_include_directories(zstd PUBLIC extern/zstd/lib)
target_compile_definitions(zstd PRIVATE ZSTD_MULTITHREAD)

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
target_compile_definitions(bc7enc_rdo PRIVATE MINIZ_HEADER_FILE_ONLY)
target_link_libraries(bc7enc_rdo PUBLIC miniz)

# Clay UI =====================================
add_library(clay STATIC
        extern/clay/clay.cpp
        extern/clay/clay.h
)
target_include_directories(clay PUBLIC extern/clay)

# tlsf =======================================
add_library(tlsf STATIC
        extern/tlsf/tlsf.c
        extern/tlsf/tlsf.h
)
target_include_directories(tlsf PUBLIC extern/tlsf)

# Tracy =======================================
if (WILL_ENGINE_ENABLE_PROFILER)
    add_library(tracy SHARED
            extern/tracy/public/TracyClient.cpp
    )
    target_compile_definitions(tracy PRIVATE
            TRACY_ENABLE
            TRACY_EXPORTS
    )

    add_library(tracy_interface INTERFACE)
    target_link_libraries(tracy_interface INTERFACE tracy)
    target_compile_definitions(tracy_interface INTERFACE
            TRACY_ENABLE
            TRACY_IMPORTS
    )
else ()
    add_library(tracy STATIC
            extern/tracy/public/TracyClient.cpp
    )

    add_library(tracy_interface INTERFACE)
    target_link_libraries(tracy_interface INTERFACE tracy)
endif ()

target_include_directories(tracy PUBLIC
        extern/tracy/public
)

# Shaders =====================================
set_property(GLOBAL PROPERTY SHADER_INCLUDE_DIRS "")
function(add_shader_include_directory DIR)
    get_filename_component(DIR_ABS ${DIR} ABSOLUTE)
    set_property(GLOBAL APPEND PROPERTY SHADER_INCLUDE_DIRS ${DIR_ABS})
endfunction()
add_shader_include_directory("${CMAKE_SOURCE_DIR}/shaders")
add_shader_include_directory("${CMAKE_SOURCE_DIR}/shaders/include")
add_shader_include_directory("${CMAKE_SOURCE_DIR}/src/render/shaders")

# Used to define dependencies for building (not actually the includes)
set_property(GLOBAL PROPERTY SHADER_INCLUDE_FILES "")
function(add_shader_include INCLUDE_FILE)
    get_filename_component(INCLUDE_ABS ${INCLUDE_FILE} ABSOLUTE)
    set_property(GLOBAL APPEND PROPERTY SHADER_INCLUDE_FILES ${INCLUDE_ABS})
endfunction()
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/visibility_shading_common.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/visibility_lighting_common.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/shader_constants.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/shader_functions.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/ui_functions.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/pbr_functions.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/packing_functions.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/bindless_declarations.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/procedural_texture_declarations.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/random_common.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/restir_common.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/regir_hash.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/gpu_debug_draw.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/ddgi_common.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/world_cache_hash.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/sobol_common.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/relax_utils.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/reblur_utils.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/reblur_common.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/sigma_utils.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/hit_shading_common.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/binning_common.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/punctual_lighting.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/limits_declarations.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/sobol_directions.slang")

file(GLOB INTEROP_DEP_FILES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/render/shaders/*.h"
        "${CMAKE_SOURCE_DIR}/src/render/shaders/*.slang")
foreach(f ${INTEROP_DEP_FILES})
    add_shader_include(${f})
endforeach()

file(GLOB_RECURSE WILL_ENGINE_SHADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/shaders/*.slang")

set(WILL_ENGINE_SHADER_FILES "")
foreach(shader ${WILL_ENGINE_SHADER_SOURCES})
    string(FIND "${shader}" "/shaders/include/" _shader_is_include)
    if(NOT _shader_is_include EQUAL -1)
        continue()
    endif()
    list(APPEND WILL_ENGINE_SHADER_FILES ${shader})
endforeach()
compile_shaders(will-engine ${WILL_ENGINE_SHADER_FILES})

# Post-Build Commands =========================
add_custom_command(TARGET will-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:will-engine>
)
add_custom_command(TARGET will-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3_mixer::SDL3_mixer>
        $<TARGET_FILE_DIR:will-engine>
)
add_custom_command(TARGET will-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:tracy>
        $<TARGET_FILE_DIR:will-engine>
)