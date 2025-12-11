add_library(render STATIC
        render_thread.cpp
        render_thread.h
        vk_context.cpp
        vk_context.h
        vk_swapchain.cpp
        vk_swapchain.h
        vk_render_targets.cpp
        vk_render_targets.h
        shaders/shader_interop.h
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
)

target_include_directories(render PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(render PUBLIC
        SDL3::SDL3
        volk
        VulkanMemoryAllocator
        vk-bootstrap::vk-bootstrap
        spdlog::spdlog
        enkiTS
)