cmake_minimum_required(VERSION 4.0)
project(will-engine)

set(CMAKE_CXX_STANDARD 23)

# x86-64-v3 baseline (AVX2/FMA/F16C/BMI)
# We can make the assumption users have support for this w/ the GPU target of SM 6.6
if (MSVC)
    add_compile_options(/arch:AVX2 /fp:contract)
else ()
    add_compile_options(-march=x86-64-v3)
endif ()


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
# __FUNCTION__ in a lambda is bare operator() on Clang/GCC
# __FUNCSIG__/__PRETTY_FUNCTION__ keep the enclosing function and are not diagnosed.
if (MSVC)
    add_compile_definitions(SPDLOG_FUNCTION=__FUNCSIG__)
else ()
    add_compile_definitions(SPDLOG_FUNCTION=__PRETTY_FUNCTION__)
endif ()
if (MSVC)
    add_compile_definitions($<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
    add_compile_options($<$<CONFIG:Debug>:/JMC->)
    add_link_options(/PDBPAGESIZE:8192)
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
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/meshoptimizer)

# NRD (reference denoiser). Source tree at tag v4.17.4 is provided out-of-band into extern/NRD;
# MathLib/ShaderMake are consumed from extern/NRD/External via FetchContent source-dir overrides (no network at configure).
set(NRD_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(NRD_EMBEDS_SPIRV_SHADERS ON CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXIL_SHADERS OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "" FORCE)
# Engine does not enable VK_KHR_compute_shader_derivatives
set(NRD_SUPPORTS_QUAD_INTRINSICS OFF CACHE BOOL "" FORCE)
set(NRD_NORMAL_ENCODING "2" CACHE STRING "" FORCE)
set(NRD_ROUGHNESS_ENCODING "1" CACHE STRING "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_SHADERMAKE "${CMAKE_SOURCE_DIR}/extern/NRD/External/ShaderMake" CACHE PATH "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_MATHLIB "${CMAKE_SOURCE_DIR}/extern/NRD/External/MathLib" CACHE PATH "" FORCE)
add_subdirectory(extern/NRD)

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
set(USE_SSE4_1 ON)
set(USE_SSE4_2 ON)
set(USE_AVX ON)
set(USE_AVX2 ON)
set(USE_LZCNT ON)
set(USE_TZCNT ON)
set(USE_F16C ON)
set(USE_FMADD ON)
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

# BC7E (ISPC-vectorized BC7 encoder)
find_program(ISPC_EXECUTABLE ispc)
if (ISPC_EXECUTABLE)
    set(BC7E_GEN_DIR ${CMAKE_BINARY_DIR}/bc7e)
    file(MAKE_DIRECTORY ${BC7E_GEN_DIR})
    if (WIN32)
        set(BC7E_OBJ ${BC7E_GEN_DIR}/bc7e.obj)
    else ()
        set(BC7E_OBJ ${BC7E_GEN_DIR}/bc7e.o)
    endif ()
    add_custom_command(
            OUTPUT ${BC7E_OBJ} ${BC7E_GEN_DIR}/bc7e_ispc.h
            COMMAND ${ISPC_EXECUTABLE} -O2 ${CMAKE_SOURCE_DIR}/extern/bc7enc_rdo/bc7e.ispc -o ${BC7E_OBJ} -h ${BC7E_GEN_DIR}/bc7e_ispc.h --target=avx2 --opt=fast-math --opt=disable-assertions
            DEPENDS ${CMAKE_SOURCE_DIR}/extern/bc7enc_rdo/bc7e.ispc
    )
    target_sources(bc7enc_rdo PRIVATE ${BC7E_OBJ})
    target_include_directories(bc7enc_rdo PUBLIC ${BC7E_GEN_DIR})
    target_compile_definitions(bc7enc_rdo PUBLIC SUPPORT_BC7E=1)
    message(STATUS "BC7E enabled via ${ISPC_EXECUTABLE}")
else ()
    message(STATUS "ispc not found; BC7 encoding falls back to scalar bc7enc")
endif ()

add_library(harfbuzz STATIC
        extern/harfbuzz/src/harfbuzz.cc
        extern/harfbuzz/src/hb-gpu.cc
        extern/harfbuzz/src/hb-gpu-draw.cc
        extern/harfbuzz/src/hb-gpu.h
)
target_include_directories(harfbuzz PUBLIC extern/harfbuzz/src)
target_compile_definitions(harfbuzz PRIVATE
        hb_malloc_impl=hb_malloc_impl
        hb_calloc_impl=hb_calloc_impl
        hb_realloc_impl=hb_realloc_impl
        hb_free_impl=hb_free_impl
)
if (MSVC)
    target_compile_options(harfbuzz PRIVATE /bigobj)
endif ()

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
# Helper modules are identified by filename suffix, not folder: *_common / *_functions / *_declarations / *_constants.
# Helpers are excluded from entry-point compilation and registered as rebuild dependencies instead;
# every directory containing a .slang becomes an import search root, so helpers live next to their consumers.
set_property(GLOBAL PROPERTY SHADER_INCLUDE_DIRS "")
function(add_shader_include_directory DIR)
    get_filename_component(DIR_ABS ${DIR} ABSOLUTE)
    set_property(GLOBAL APPEND PROPERTY SHADER_INCLUDE_DIRS ${DIR_ABS})
endfunction()
add_shader_include_directory("${CMAKE_SOURCE_DIR}/src/render/shaders")

set_property(GLOBAL PROPERTY SHADER_INCLUDE_FILES "")
function(add_shader_include INCLUDE_FILE)
    get_filename_component(INCLUDE_ABS ${INCLUDE_FILE} ABSOLUTE)
    set_property(GLOBAL APPEND PROPERTY SHADER_INCLUDE_FILES ${INCLUDE_ABS})
endfunction()

file(GLOB INTEROP_DEP_FILES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/render/shaders/*.h"
        "${CMAKE_SOURCE_DIR}/src/render/shaders/*.slang")
foreach(f ${INTEROP_DEP_FILES})
    add_shader_include(${f})
endforeach()

file(GLOB_RECURSE WILL_ENGINE_SHADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/shaders/*.slang")

set(WILL_ENGINE_SHADER_FILES "")
set(WILL_ENGINE_SHADER_DIRS "")
foreach(shader ${WILL_ENGINE_SHADER_SOURCES})
    get_filename_component(_shader_dir ${shader} DIRECTORY)
    list(APPEND WILL_ENGINE_SHADER_DIRS ${_shader_dir})
    if(shader MATCHES "_(common|functions|declarations|constants)\\.slang$")
        add_shader_include(${shader})
        continue()
    endif()
    list(APPEND WILL_ENGINE_SHADER_FILES ${shader})
endforeach()
list(REMOVE_DUPLICATES WILL_ENGINE_SHADER_DIRS)
foreach(_shader_dir ${WILL_ENGINE_SHADER_DIRS})
    add_shader_include_directory(${_shader_dir})
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