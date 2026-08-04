add_library(render STATIC
        render_thread.cpp
        render_thread.h
        light_bvh.cpp
        light_bvh.h
        resource_manager.cpp
        resource_manager.h
        frame_resources.h
        render_config.h
        vulkan/vk_context.cpp
        vulkan/vk_context.h
        vulkan/vk_swapchain.cpp
        vulkan/vk_swapchain.h
        vulkan/vk_synchronization.cpp
        vulkan/vk_synchronization.h
        vulkan/vk_utils.cpp
        vulkan/vk_utils.h
        vulkan/vk_config.cpp
        vulkan/vk_config.h
        vulkan/vk_resources.cpp
        vulkan/vk_resources.h
        vulkan/vk_helpers.cpp
        vulkan/vk_helpers.h
        vulkan/vk_render_extents.cpp
        vulkan/vk_render_extents.h
        vulkan/vk_imgui_wrapper.cpp
        vulkan/vk_imgui_wrapper.h
        types/render_types.cpp
        types/render_types.h
        shaders/common_interop.h
        descriptors/vk_bindless_resources_storage.cpp
        descriptors/vk_bindless_resources_storage.h
        descriptors/vk_bindless_resources_sampler_images.cpp
        descriptors/vk_bindless_resources_sampler_images.h
        descriptors/vk_bindless_resources_combined.cpp
        descriptors/vk_bindless_resources_combined.h
        descriptors/vk_descriptors.cpp
        descriptors/vk_descriptors.h
        types/vk_types.cpp
        types/vk_types.h
        pipelines/graphics_pipeline_builder.cpp
        pipelines/graphics_pipeline_builder.h
        shaders/model_interop.h
        shaders/constants_interop.h
        types/cubemap_asset.cpp
        types/cubemap_asset.h
        render-graph/render_graph.cpp
        render-graph/render_graph.h
        render-graph/render_graph_resources.cpp
        render-graph/render_graph_resources.h
        render-graph/render_pass.cpp
        render-graph/render_pass.h
        descriptors/vk_bindless_transient_rdg_resources.cpp
        descriptors/vk_bindless_transient_rdg_resources.h
        render-graph/render_graph_config.h
        shaders/push_constant_interop.h
        shaders/instancing_interop.h
        shaders/lights_interop.h
        pipelines/pipeline_manager.cpp
        pipelines/pipeline_manager.h
        pipelines/pipeline_category.cpp
        pipelines/pipeline_category.h
        render-view/render_view_helpers.cpp
        render-view/render_view_helpers.h
        pipelines/pipeline_data.cpp
        pipelines/pipeline_data.h
        shaders/text_interop.h

        interface/render_interface.cpp
        interface/render_interface.h
        stb_write_impl.cpp
        systems/render_screen_capture.cpp
        systems/render_screen_capture.h
        renderer.cpp
        renderer.h
        passes/geometry_passes.cpp
        passes/geometry_passes.h
        passes/lighting_passes.cpp
        passes/lighting_passes.h
        passes/restir_passes.cpp
        passes/restir_passes.h
        passes/reflection_passes.cpp
        passes/reflection_passes.h
        passes/denoising_passes.cpp
        passes/denoising_passes.h
        passes/nrd_denoiser.cpp
        passes/nrd_denoiser.h
        passes/ambient_occlusion_passes.cpp
        passes/ambient_occlusion_passes.h
        passes/shadow_passes.cpp
        passes/shadow_passes.h
        passes/scene_passes.cpp
        passes/scene_passes.h
        passes/anti_aliasing_passes.cpp
        passes/anti_aliasing_passes.h
        passes/ui_passes.cpp
        passes/ui_passes.h
        passes/debug_passes.cpp
        passes/debug_passes.h
        passes/ddgi_passes.cpp
        passes/ddgi_passes.h
        shaders/ddgi_interop.h
        passes/radiance_cache_passes.cpp
        passes/radiance_cache_passes.h
        shaders/radiance_cache_interop.h
        passes/final_gather_passes.cpp
        passes/final_gather_passes.h
        post-processing/post_processing.cpp
        post-processing/post_processing.h
        renderer_types.h
        render_utils.h
        render_utils.cpp
        vulkan/vk_pipeline_stats.cpp
        vulkan/vk_pipeline_stats.h
        vulkan/vk_gpu_timestamp_query.cpp
        vulkan/vk_gpu_timestamp_query.h
        descriptors/procedural_texture_generate_resources.cpp
        passes/raytracing_passes.cpp
)

target_include_directories(render PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(render PUBLIC
        core
        asset-load
        platform
        SDL3::SDL3
        volk
        VulkanMemoryAllocator
        vk-bootstrap::vk-bootstrap
        spdlog::spdlog
        enkiTS
        offsetAllocator
        ktx
        miniz
        lz4
        zstd
        imgui
        tracy_interface
        bc7enc_rdo
        clay
        NRD
)

if(WILL_ENGINE_BUILD_EDITOR)
    target_link_libraries(render PUBLIC
            editor
    )
endif()

find_program(SLANGC slangc REQUIRED)

function(compile_shader TARGET SHADER_SOURCE)
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME_WE)
    get_filename_component(SHADER_SOURCE_ABS ${SHADER_SOURCE} ABSOLUTE)
    get_filename_component(SHADER_INCLUDE_DIR ${SHADER_SOURCE_ABS} DIRECTORY)

    set(SHADER_OUTPUT ${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv)
    get_property(INCLUDE_FILES GLOBAL PROPERTY SHADER_INCLUDE_FILES)

    get_property(INCLUDE_DIRS GLOBAL PROPERTY SHADER_INCLUDE_DIRS)
    set(SLANG_INCLUDE_FLAGS "")
    foreach(dir ${INCLUDE_DIRS})
        list(APPEND SLANG_INCLUDE_FLAGS -I ${dir})
    endforeach()

    add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
            COMMAND ${SLANGC} ${SHADER_SOURCE_ABS}
            -target spirv
            -fvk-use-entrypoint-name
            -I ${SHADER_INCLUDE_DIR}
            ${SLANG_INCLUDE_FLAGS}
            -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_SOURCE_ABS} ${INCLUDE_FILES}
            COMMENT "Compiling shader: ${SHADER_NAME}.spv"
            VERBATIM
    )

    set_property(TARGET ${TARGET} APPEND PROPERTY SHADER_OUTPUTS ${SHADER_OUTPUT})
endfunction()


function(compile_shaders TARGET)
    foreach(SHADER_SOURCE ${ARGN})
        compile_shader(${TARGET} ${SHADER_SOURCE})
    endforeach()

    # Create single custom target for all shaders
    get_property(SHADER_OUTPUTS TARGET ${TARGET} PROPERTY SHADER_OUTPUTS)
    add_custom_target(${TARGET}_shaders ALL DEPENDS ${SHADER_OUTPUTS})
    add_dependencies(${TARGET} ${TARGET}_shaders)
endfunction()