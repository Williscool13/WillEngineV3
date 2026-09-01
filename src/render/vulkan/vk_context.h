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
    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProps{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    bool bREBAR{false};
};

struct VulkanContext
{
    static DeviceInfo deviceInfo;
    /** Set before context creation to make the device report no REBAR, so the staged upload path can be exercised on hardware that has it. */
    static bool bForceNoREBAR;

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

    // May be null
    VkQueue computeQueue{};
    uint32_t computeQueueFamily{UINT32_MAX};

    uint32_t bufferSharingFamilies[3]{};
    uint32_t bufferSharingFamilyCount{0};

    /**
     * Buffers are always VK_SHARING_MODE_CONCURRENT engine-wide across all distinct queue families.
     * @param info buffer create info to patch
     * @returns the info with sharingMode and the family list applied (EXCLUSIVE fallback if fewer than 2 distinct families)
     */
    [[nodiscard]] VkBufferCreateInfo ApplyBufferSharing(VkBufferCreateInfo info) const
    {
        if (bufferSharingFamilyCount >= 2) {
            info.sharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = bufferSharingFamilyCount;
            info.pQueueFamilyIndices = bufferSharingFamilies;
        }
        return info;
    }

    // Optional Extensions
    bool bMaintenance9Enabled{false};
    bool bMeshShaderQueriesEnabled{false};
    bool bPipelineExecutablePropertiesEnabled{false};

    VmaVulkanFunctions vulkanFunctions{};

    VkAllocationCallbacks hostAllocationCallbacks{};

    [[nodiscard]] const VkAllocationCallbacks* HostAllocCallbacks() const { return &hostAllocationCallbacks; }

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
