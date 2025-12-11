//
// Created by William on 2025-12-11.
//

#ifndef WILLENGINETESTBED_VULKAN_CONTEXT_H
#define WILLENGINETESTBED_VULKAN_CONTEXT_H

#include <volk.h>
#include <vk_mem_alloc.h>

struct SDL_Window;

namespace Render
{
struct DeviceInfo
{
    VkPhysicalDeviceProperties2 properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProps{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};
    VkPhysicalDeviceMeshShaderPropertiesEXT meshShaderProps{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT};
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

    VulkanContext() = default;

    explicit VulkanContext(SDL_Window* window);

    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;

    VulkanContext(VulkanContext&&) = delete;
};
} // Renderer

#endif //WILLENGINETESTBED_VULKAN_CONTEXT_H
