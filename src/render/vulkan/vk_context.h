//
// Created by William on 2025-12-11.
//

#ifndef WILLENGINETESTBED_VULKAN_CONTEXT_H
#define WILLENGINETESTBED_VULKAN_CONTEXT_H

#include <volk.h>
#include <vk_mem_alloc.h>
#include <tracy/TracyVulkan.hpp>

struct SDL_Window;

namespace Core
{
class MemoryManager;
}

namespace Render
{
struct DeviceInfo
{
    VkPhysicalDeviceProperties2 properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProps{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};
    VkPhysicalDeviceMeshShaderPropertiesEXT meshShaderProps{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT};
    VkPhysicalDeviceSubgroupProperties subgroupProps{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
};

struct VulkanContext
{
    static DeviceInfo deviceInfo;

    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physicalDevice{};
    VkDevice device{};
    VkQueue graphicsQueue{};
    uint32_t graphicsQueueFamily{};
    VkQueue transferQueue{};
    uint32_t transferQueueFamily{};
    VmaAllocator allocator{};
    VkDebugUtilsMessengerEXT debugMessenger{};

    // Optional Extensions
    bool bMaintenance9Enabled{false};

    VmaVulkanFunctions vulkanFunctions{};

    VulkanContext() = default;

    VulkanContext(SDL_Window* window, Core::MemoryManager& memoryManager);

    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;

    VulkanContext(VulkanContext&&) = delete;

#if PROFILER_ENABLED
    TracyVkCtx tracyContext;
#endif
};
} // Renderer

#endif //WILLENGINETESTBED_VULKAN_CONTEXT_H
