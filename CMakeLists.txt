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
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/common_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/include/common_interop.slang
)

preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/model_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/include/model_interop.slang
)
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/instancing_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/include/instancing_interop.slang
)
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/lights_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/include/lights_interop.slang
)
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/constants_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/include/constants_interop.slang
)
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/push_constant_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/include/push_constant_interop.slang
)
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/text_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/include/text_interop.slang
)
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/ui_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/include/ui_interop.slang
)
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/restir_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/include/restir_interop.slang
)
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/relax_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/include/relax_interop.slang
)
preprocess_shader_interop(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/reblur_interop.h
        ${CMAKE_SOURCE_DIR}/shaders/include/reblur_interop.slang
)

copy_shader_macros(
        ${CMAKE_SOURCE_DIR}/src/render/shaders/restir_features_macros.h
        ${CMAKE_SOURCE_DIR}/shaders/include/restir_features_macros.slang
)


get_property(INTEROP_FILES GLOBAL PROPERTY SHADER_INTEROP_FILES)
add_custom_target(shader_interop DEPENDS ${INTEROP_FILES})

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
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/sobol_common.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/relax_utils.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/reblur_utils.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/reblur_common.slang")
add_shader_include("${CMAKE_SOURCE_DIR}/shaders/include/sigma_utils.slang")

compile_shaders(will-engine
        ${CMAKE_SOURCE_DIR}/shaders/debug_visualize.slang compute ComputeMain debug_visualize
        ${CMAKE_SOURCE_DIR}/shaders/depth_copy.slang compute ComputeMain depth_copy

        # Instancing (Either 1->2->scan->1->2 or 1->scan->2)
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputeInstanceLOD instancing_instance_lod
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputePrefixSumUpsweep1 instancing_prefix_sum_up_1
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputePrefixSumUpsweep2 instancing_prefix_sum_up_2
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputePrefixSumScanBlocks instancing_scan_blocks
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputePrefixSumDownsweep1 instancing_prefix_sum_down_1
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputePrefixSumDownsweep2 instancing_prefix_sum_down_2
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputeTotalMeshletCount instancing_total_meshlet_count
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputeExpandInstancesToMeshlets instancing_expand_instance_to_meshlet
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputeMeshletVisibilityPrefixSumUpsweep1 instancing_meshlet_visibility_prefix_sum_up_1
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputeMeshletVisibilityPrefixSumDownsweep2 instancing_meshlet_visibility_prefix_sum_down_2
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputeCompactedMeshletDispatch instancing_compacted_meshlet_dispatch
        ${CMAKE_SOURCE_DIR}/shaders/instancing.slang compute ComputeMaxMeshletCount instancing_max_meshlet_count

        # Visibility Buffer
        #   Geometry
        ${CMAKE_SOURCE_DIR}/shaders/geometry/geometry_visibility_buffer.slang mesh MeshMain visibility_buffer
        ${CMAKE_SOURCE_DIR}/shaders/geometry/geometry_visibility_buffer.slang fragment FragmentMain visibility_buffer
        ${CMAKE_SOURCE_DIR}/shaders/visibility_buffer/visibility_bucketing.slang compute VisibilityBufferBarycentricDerivative visibility_buffer_barycentric_derivative
        #   Shade Dispatch Bucketing
        ${CMAKE_SOURCE_DIR}/shaders/visibility_buffer/visibility_bucketing.slang compute ComputeShadeDispatchBucketing visibility_bucketing_bounds_calculation
        ${CMAKE_SOURCE_DIR}/shaders/visibility_buffer/visibility_bucketing.slang compute ComputeShadeDispatchBucketingResolve visibility_shading_bucketing_resolve
        ${CMAKE_SOURCE_DIR}/shaders/visibility_buffer/visibility_bucketing.slang compute ComputeLightDispatchBucketingResolve visibility_lighting_bucketing_resolve
        ${CMAKE_SOURCE_DIR}/shaders/visibility_buffer/visibility_bucketing.slang compute ComputeBucketDispatchCount bucketing_dispatch_count
        #   "Fragment Shaders"
        ${CMAKE_SOURCE_DIR}/shaders/shading/shading_bucket_visualize.slang compute ComputeMain shading_bucket_visualize
        ${CMAKE_SOURCE_DIR}/shaders/shading/lighting_bucket_visualize.slang compute ComputeMain lighting_bucket_visualize
        ${CMAKE_SOURCE_DIR}/shaders/shading/shading_default_lit.slang compute ComputeMain shading_default_lit
        ${CMAKE_SOURCE_DIR}/shaders/shading/shading_error_unlit.slang compute ComputeMain shading_error_unlit
        #   "Lighting Shaders"
        ${CMAKE_SOURCE_DIR}/shaders/lighting/lighting_pbr.slang compute ComputeMain lighting_pbr
        ${CMAKE_SOURCE_DIR}/shaders/lighting/lighting_pbr_restir.slang compute ComputeMain lighting_pbr_restir
        ${CMAKE_SOURCE_DIR}/shaders/lighting/lighting_ground_truth.slang compute ComputeMain lighting_ground_truth
        ${CMAKE_SOURCE_DIR}/shaders/lighting/lighting_toon.slang compute ComputeMain lighting_toon
        ${CMAKE_SOURCE_DIR}/shaders/lighting/lighting_unlit.slang compute ComputeMain lighting_unlit
        ${CMAKE_SOURCE_DIR}/shaders/lighting/directional_light.slang compute ComputeMain directional_light

        ${CMAKE_SOURCE_DIR}/shaders/raytracing/rt_shadow_test.slang compute ComputeMain rt_shadow_test
        ${CMAKE_SOURCE_DIR}/shaders/raytracing/rt_ground_truth_di.slang compute ComputeMain rt_ground_truth_di
        ${CMAKE_SOURCE_DIR}/shaders/raytracing/rt_sun_shadow.slang compute ComputeMain rt_sun_shadow

        ${CMAKE_SOURCE_DIR}/shaders/denoising/sigma/sigma_classify_tiles.slang compute ComputeMain sigma_classify_tiles
        ${CMAKE_SOURCE_DIR}/shaders/denoising/sigma/sigma_smooth_tiles.slang compute ComputeMain sigma_smooth_tiles
        ${CMAKE_SOURCE_DIR}/shaders/denoising/sigma/sigma_shadow_blur.slang compute ComputeMain sigma_shadow_blur
        ${CMAKE_SOURCE_DIR}/shaders/denoising/sigma/sigma_shadow_temporal.slang compute ComputeMain sigma_shadow_temporal

        # ReSTIR
        ${CMAKE_SOURCE_DIR}/shaders/lighting/restir-di/restir_di_transform_lights.slang compute ComputeMain restir_di_transform_lights
        ${CMAKE_SOURCE_DIR}/shaders/lighting/restir-di/restir_di_spatial.slang compute ComputeMain restir_di_spatial
        ${CMAKE_SOURCE_DIR}/shaders/lighting/restir-di/restir_di_base_regir.slang compute ComputeMain restir_di_base_regir
        ${CMAKE_SOURCE_DIR}/shaders/lighting/restir-di/restir_di_temporal.slang compute ComputeMain restir_di_temporal
        ${CMAKE_SOURCE_DIR}/shaders/lighting/restir-di/restir_remodulate.slang compute ReSTIRRemodulateMain restir_remodulate

        ${CMAKE_SOURCE_DIR}/shaders/lighting/restir-di/regir_touch.slang compute ComputeMain regir_touch
        ${CMAKE_SOURCE_DIR}/shaders/lighting/restir-di/regir_build_indirect.slang compute ComputeMain regir_build_indirect
        ${CMAKE_SOURCE_DIR}/shaders/lighting/restir-di/regir_presample_tiles.slang compute ComputeMain regir_presample_tiles
        ${CMAKE_SOURCE_DIR}/shaders/lighting/restir-di/regir_fill.slang compute ComputeMain regir_fill
        ${CMAKE_SOURCE_DIR}/shaders/lighting/restir-di/restir_boiling_filter.slang compute ComputeMain restir_boiling_filter
        ${CMAKE_SOURCE_DIR}/shaders/lighting/restir-di/restir_confidence_gradient.slang compute ComputeMain restir_confidence_gradient
        ${CMAKE_SOURCE_DIR}/shaders/lighting/restir-di/restir_confidence_resolve.slang compute ComputeMain restir_confidence_resolve

        ${CMAKE_SOURCE_DIR}/shaders/shadows_resolve.slang compute ComputeMain shadows_resolve

        # Sprite Rendering
        ${CMAKE_SOURCE_DIR}/shaders/sprites.slang mesh MeshMain sprites
        ${CMAKE_SOURCE_DIR}/shaders/sprites.slang fragment FragmentMain sprites

        # Text Rendering
        ${CMAKE_SOURCE_DIR}/shaders/text/text_default.slang mesh MeshMain default_text
        ${CMAKE_SOURCE_DIR}/shaders/text/text_default.slang fragment FragmentMain default_text

        # UI Rendering
        ${CMAKE_SOURCE_DIR}/shaders/ui/ui_text_default.slang vertex VertexMain ui_text_default
        ${CMAKE_SOURCE_DIR}/shaders/ui/ui_text_default.slang fragment FragmentMain ui_text_default
        ${CMAKE_SOURCE_DIR}/shaders/ui/ui_image_default.slang vertex VertexMain ui_image_default
        ${CMAKE_SOURCE_DIR}/shaders/ui/ui_image_default.slang fragment FragmentMain ui_image_default
        ${CMAKE_SOURCE_DIR}/shaders/ui/ui_rect_default.slang vertex VertexMain ui_rect_default
        ${CMAKE_SOURCE_DIR}/shaders/ui/ui_rect_default.slang fragment FragmentMain ui_rect_default
        ${CMAKE_SOURCE_DIR}/shaders/ui/ui_border_default.slang vertex VertexMain ui_border_default
        ${CMAKE_SOURCE_DIR}/shaders/ui/ui_border_default.slang fragment FragmentMain ui_border_default

        # Debug / Editor Rendering
        ${CMAKE_SOURCE_DIR}/shaders/debug_render.slang fragment FragmentMain debug_render
        ${CMAKE_SOURCE_DIR}/shaders/debug_render.slang mesh MeshMain debug_render
        ${CMAKE_SOURCE_DIR}/shaders/selection_outline.slang compute ComputeMain selection_outline


        # Deprecated
        ${CMAKE_SOURCE_DIR}/shaders/portal_rendering.slang fragment FragmentPortalComposite portal_composite



        ${CMAKE_SOURCE_DIR}/shaders/anti-aliasing/temporal_antialiasing.slang compute ComputeMain temporal_antialiasing
        ${CMAKE_SOURCE_DIR}/shaders/anti-aliasing/naive_temporal_antialiasing.slang compute ComputeMain naive_temporal_antialiasing
        ${CMAKE_SOURCE_DIR}/shaders/donut_taa.slang compute ComputeMain donut_taa
        ${CMAKE_SOURCE_DIR}/shaders/anti-aliasing/subpixel_morphological_anti_aliasing_edge_detection.slang compute LumaEdgeDetectionMain smaa_luma_edge_detection
        ${CMAKE_SOURCE_DIR}/shaders/anti-aliasing/subpixel_morphological_anti_aliasing_edge_detection.slang compute ColorEdgeDetectionMain smaa_color_edge_detection
        ${CMAKE_SOURCE_DIR}/shaders/anti-aliasing/subpixel_morphological_anti_aliasing_edge_detection.slang compute DepthEdgeDetectionMain smaa_depth_edge_detection
        ${CMAKE_SOURCE_DIR}/shaders/anti-aliasing/subpixel_morphological_anti_aliasing_blend_weight.slang compute BlendWeightMain smaa_blend_weight
        ${CMAKE_SOURCE_DIR}/shaders/anti-aliasing/subpixel_morphological_anti_aliasing_neighborhood_blend.slang compute NeighborhoodBlendMain smaa_neighborhood_blend
        ${CMAKE_SOURCE_DIR}/shaders/ambient-occlusion/ground_truth_ambient_occlusion.slang compute ComputeGTAODepthPrepass gtao_depth_prepass
        ${CMAKE_SOURCE_DIR}/shaders/ambient-occlusion/ground_truth_ambient_occlusion.slang compute ComputeGTAOMain gtao_main
        ${CMAKE_SOURCE_DIR}/shaders/ambient-occlusion/ground_truth_ambient_occlusion.slang compute ComputeGTAODenoise gtao_denoise
        ${CMAKE_SOURCE_DIR}/shaders/denoising/relax/relax_generate_viewz.slang compute RelaxGenerateViewZMain relax_generate_viewz
        ${CMAKE_SOURCE_DIR}/shaders/denoising/relax/relax_classify_tiles.slang compute RelaxClassifyTilesMain relax_classify_tiles
        ${CMAKE_SOURCE_DIR}/shaders/denoising/relax/relax_prepass.slang compute RelaxPrepassMain relax_prepass
        ${CMAKE_SOURCE_DIR}/shaders/denoising/relax/relax_temporal_accumulation.slang compute RelaxTemporalAccumulationMain relax_temporal_accumulation
        ${CMAKE_SOURCE_DIR}/shaders/denoising/relax/relax_history_fix.slang compute RelaxHistoryFixMain relax_history_fix
        ${CMAKE_SOURCE_DIR}/shaders/denoising/relax/relax_history_clamping.slang compute RelaxHistoryClampingMain relax_history_clamping
        ${CMAKE_SOURCE_DIR}/shaders/denoising/relax/relax_atrous.slang compute RelaxAtrousMain relax_atrous
        ${CMAKE_SOURCE_DIR}/shaders/denoising/relax/relax_antifirefly.slang compute RelaxAntiFireflyMain relax_antifirefly
        ${CMAKE_SOURCE_DIR}/shaders/denoising/reblur/reblur_pack.slang compute ReblurPackMain reblur_pack
        ${CMAKE_SOURCE_DIR}/shaders/denoising/reblur/reblur_generate_viewz.slang compute ReblurGenerateViewZMain reblur_generate_viewz
        ${CMAKE_SOURCE_DIR}/shaders/denoising/reblur/reblur_classify_tiles.slang compute ReblurClassifyTilesMain reblur_classify_tiles
        ${CMAKE_SOURCE_DIR}/shaders/denoising/reblur/reblur_prepass.slang compute ReblurPrepassMain reblur_prepass
        ${CMAKE_SOURCE_DIR}/shaders/denoising/reblur/reblur_temporal_accumulation.slang compute ReblurTemporalAccumulationMain reblur_temporal_accumulation
        ${CMAKE_SOURCE_DIR}/shaders/denoising/reblur/reblur_history_fix.slang compute ReblurHistoryFixMain reblur_history_fix
        ${CMAKE_SOURCE_DIR}/shaders/denoising/reblur/reblur_blur.slang compute ReblurBlurMain reblur_blur
        ${CMAKE_SOURCE_DIR}/shaders/denoising/reblur/reblur_stabilization.slang compute ReblurStabilizationMain reblur_stabilization
        ${CMAKE_SOURCE_DIR}/shaders/post-process/tonemapping.slang compute ComputeSDRTonemap tonemap_sdr
        ${CMAKE_SOURCE_DIR}/shaders/post-process/exposure.slang compute ComputeBuildHistogramExposure exposure_build_histogram
        ${CMAKE_SOURCE_DIR}/shaders/post-process/exposure.slang compute ComputeAverageExposure exposure_calculate_average
        ${CMAKE_SOURCE_DIR}/shaders/post-process/motion_blur.slang compute ComputeMotionBlurTileMax motion_blur_tile_max
        ${CMAKE_SOURCE_DIR}/shaders/post-process/motion_blur.slang compute ComputeMotionBlurNeighborMax motion_blur_neighbor_max
        ${CMAKE_SOURCE_DIR}/shaders/post-process/motion_blur.slang compute ComputeMotionBlurReconstruction motion_blur_reconstruction
        ${CMAKE_SOURCE_DIR}/shaders/post-process/bloom.slang compute ComputeBloomThreshold bloom_threshold
        ${CMAKE_SOURCE_DIR}/shaders/post-process/bloom.slang compute ComputeBloomDownsample bloom_downsample
        ${CMAKE_SOURCE_DIR}/shaders/post-process/bloom.slang compute ComputeBloomUpsample bloom_upsample
        ${CMAKE_SOURCE_DIR}/shaders/post-process/vignette_aberration.slang compute ComputeVignetteAberration vignette_aberration
        ${CMAKE_SOURCE_DIR}/shaders/post-process/film_grain.slang compute ComputeFilmGrain film_grain
        ${CMAKE_SOURCE_DIR}/shaders/post-process/sharpening.slang compute ComputeSharpening sharpening
        ${CMAKE_SOURCE_DIR}/shaders/post-process/color_grading.slang compute ComputeColorGrading color_grading
        ${CMAKE_SOURCE_DIR}/shaders/post-process/panini_projection.slang compute ComputePaniniProjection panini_projection
        ${CMAKE_SOURCE_DIR}/shaders/post-process/dither.slang compute ComputeDither dither

        ${CMAKE_SOURCE_DIR}/shaders/common_shaders.slang vertex FullscreenPassVertexMain fullscreen_pass

        ${CMAKE_SOURCE_DIR}/shaders/environment_map_generation.slang compute ComputeEquirectToCubemap ibl_equirect_to_cubemap
        ${CMAKE_SOURCE_DIR}/shaders/environment_map_generation.slang compute ComputeConvolveDiffuse ibl_convolve_diffuse
        ${CMAKE_SOURCE_DIR}/shaders/environment_map_generation.slang compute ComputePrefilterSpecular ibl_prefilter_specular
        ${CMAKE_SOURCE_DIR}/shaders/environment_map.slang fragment FragmentSkybox environment_map_skybox

        ${CMAKE_SOURCE_DIR}/shaders/brdf_lut.slang compute ComputeBRDFLUT brdf_lut_generate

        ${CMAKE_SOURCE_DIR}/shaders/procedural-textures/yellow_texture.slang compute ComputeMain yellow_texture
        ${CMAKE_SOURCE_DIR}/shaders/procedural-textures/domain_warp.slang compute ComputeMain domain_warp

        ${CMAKE_SOURCE_DIR}/shaders/anti-aliasing/subpixel_morphological_anti_aliasing_temporal_resolve.slang compute TemporalResolveMain smaa_temporal_resolve
)

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