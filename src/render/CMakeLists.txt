add_library(render STATIC
        render_thread.cpp
        render_thread.h
        frame_resources.cpp
        frame_resources.h
        render_constants.cpp
        render_constants.h
        vk_context.cpp
        vk_context.h
        vk_swapchain.cpp
        vk_swapchain.h
        vk_synchronization.cpp
        vk_synchronization.h
        vk_render_targets.cpp
        vk_render_targets.h
        vk_utils.cpp
        vk_utils.h
        vk_config.cpp
        vk_config.h
        vk_resources.cpp
        vk_resources.h
        vk_helpers.cpp
        vk_helpers.h
        vk_render_extents.cpp
        vk_render_extents.h
        vk_resource_manager.cpp
        vk_resource_manager.h
        vk_imgui_wrapper.cpp
        vk_imgui_wrapper.h
        vk_operation_ring_buffer.cpp
        vk_operation_ring_buffer.h
        types/render_types.cpp
        types/render_types.h
        shaders/shader_interop.h
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
)

target_include_directories(render PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(render PUBLIC
        core
        SDL3::SDL3
        volk
        VulkanMemoryAllocator
        vk-bootstrap::vk-bootstrap
        spdlog::spdlog
        enkiTS
        offsetAllocator
)

if(WILL_BUILD_EDITOR)
    target_link_libraries(render PUBLIC imgui)
endif()