//
// Created by William on 2025-12-11.
//

#include "vk_context.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vulkan/vk_enum_string_helper.h>
#include <spdlog/spdlog.h>

namespace Render
{
DeviceInfo VulkanContext::deviceInfo{};

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        SPDLOG_ERROR("[Vulkan] {}", pCallbackData->pMessage);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        SPDLOG_WARN("[Vulkan] {}", pCallbackData->pMessage);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        SPDLOG_INFO("[Vulkan] {}", pCallbackData->pMessage);
    }
    else {
        SPDLOG_DEBUG("[Vulkan] {}", pCallbackData->pMessage);
    }

// #ifdef _DEBUG
//     if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
//         __debugbreak();
// #endif

    return VK_FALSE;
}

VulkanContext::VulkanContext(SDL_Window* window)
{
    VkResult res = volkInitialize();
    if (res != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to initialize volk: {}", string_VkResult(res));
        SPDLOG_ERROR("Your system may not support Vulkan");
        std::abort();
    }

    vkb::InstanceBuilder builder;
    std::vector<const char*> enabledInstanceExtensions;

#ifdef NDEBUG
    bool bUseValidation = false;
#else
    bool bUseValidation = true;
    enabledInstanceExtensions.push_back("VK_EXT_debug_utils");
#endif


    auto resultInstance = builder.set_app_name("Will Engine")
            .request_validation_layers(bUseValidation)
            .set_debug_callback(VulkanDebugCallback)
            .require_api_version(1, 3)
            .enable_extensions(enabledInstanceExtensions)
            .add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT)
            .add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT)
            .build();

    vkb::Instance vkb_inst = resultInstance.value();
    instance = vkb_inst.instance;
    volkLoadInstanceOnly(instance);
    debugMessenger = vkb_inst.debug_messenger;

    SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface);

    VkPhysicalDeviceVulkan13Features features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    VkPhysicalDeviceFeatures features10{};

    // Descriptor Buffer Extension
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures = {};
    descriptorBufferFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    descriptorBufferFeatures.descriptorBuffer = VK_TRUE;

    // Task/Mesh Shader Extension
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {};
    meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    meshShaderFeatures.taskShader = VK_TRUE;
    meshShaderFeatures.meshShader = VK_TRUE;

    // Modern Rendering (Vulkan 1.3)
    features.dynamicRendering = VK_TRUE;
    features.synchronization2 = VK_TRUE;

    // GPU Driven Rendering
    features12.bufferDeviceAddress = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    features12.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    features12.drawIndirectCount = VK_TRUE;
    features10.multiDrawIndirect = VK_TRUE;

    // SV_VertexID
    features11.shaderDrawParameters = VK_TRUE;

    // uint8_t/int8_t support
    features12.shaderInt8 = VK_TRUE;

    // uint64_t/uint64 support
    features10.shaderInt16 = VK_TRUE;
    features10.shaderInt64 = VK_TRUE;

    // Gather
    features10.shaderImageGatherExtended = VK_TRUE;

    // VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR computeShaderDerivativesFeaturesKhr{};
    // computeShaderDerivativesFeaturesKhr.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR;
    // computeShaderDerivativesFeaturesKhr.computeDerivativeGroupQuads = VK_TRUE;
    // computeShaderDerivativesFeaturesKhr.computeDerivativeGroupLinear = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{vkb_inst};
    vkb::PhysicalDevice targetDevice = selector
            .set_minimum_version(1, 3)
            .set_required_features_13(features)
            .set_required_features_12(features12)
            .set_required_features_11(features11)
            .set_required_features(features10)
            .add_required_extension(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME)
            .add_required_extension(VK_EXT_MESH_SHADER_EXTENSION_NAME)
            // .add_required_extension(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME)
            .require_separate_transfer_queue()
            .set_surface(surface)
            .select()
            .value();


    // Maintenance9
    bool supportsMaintenance9 = false; {
        for (const auto& ext : targetDevice.get_available_extensions()) {
            if (strcmp(ext.c_str(), VK_KHR_MAINTENANCE_9_EXTENSION_NAME) == 0) {
                supportsMaintenance9 = true;
                break;
            }
        }
    }


    vkb::DeviceBuilder deviceBuilder{targetDevice};
    deviceBuilder.add_pNext(&descriptorBufferFeatures);
    deviceBuilder.add_pNext(&meshShaderFeatures);
    // deviceBuilder.add_pNext(&computeShaderDerivativesFeaturesKhr);
    if (supportsMaintenance9) {
        VkPhysicalDeviceMaintenance9FeaturesKHR maintenance9Features{};
        maintenance9Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR;
        maintenance9Features.maintenance9 = VK_TRUE;
        deviceBuilder.add_pNext(&maintenance9Features);
    }
    bMaintenance9Enabled = supportsMaintenance9;
    vkb::Device vkbDevice = deviceBuilder.build().value();

    device = vkbDevice.device;
    volkLoadDevice(device);
    physicalDevice = targetDevice.physical_device;

    // Queues and queue family
    {
        auto graphicsQueueResult = vkbDevice.get_queue(vkb::QueueType::graphics);
        if (!graphicsQueueResult) {
            SPDLOG_ERROR("Failed to get graphics queue: {}", graphicsQueueResult.error().message());
            SPDLOG_ERROR("Your system may not support Vulkan");
            std::abort();
        }
        graphicsQueue = graphicsQueueResult.value();

        auto graphicsQueueFamilyResult = vkbDevice.get_queue_index(vkb::QueueType::graphics);
        if (!graphicsQueueFamilyResult) {
            SPDLOG_ERROR("Failed to get graphics queue family index: {}", graphicsQueueFamilyResult.error().message());
            SPDLOG_ERROR("Your system may not support Vulkan");
            std::abort();
        }
        graphicsQueueFamily = graphicsQueueFamilyResult.value();

        auto transferQueueResult = vkbDevice.get_queue(vkb::QueueType::transfer);
        if (!transferQueueResult) {
            SPDLOG_ERROR("Failed to get transfer queue: {}", transferQueueResult.error().message());
            SPDLOG_ERROR("Your system may not support Vulkan");
            std::abort();
        }
        transferQueue = transferQueueResult.value();

        auto transferQueueFamilyResult = vkbDevice.get_queue_index(vkb::QueueType::transfer);
        if (!transferQueueFamilyResult) {
            SPDLOG_ERROR("Failed to get transfer queue family index: {}", transferQueueFamilyResult.error().message());
            SPDLOG_ERROR("Your system may not support Vulkan");
            std::abort();
        }
        transferQueueFamily = transferQueueFamilyResult.value();
    }

    if (graphicsQueueFamily == transferQueueFamily) {
        SPDLOG_ERROR("Graphics and transfer queue families are the same ({})", graphicsQueueFamily);
        SPDLOG_ERROR("Your system may not support Vulkan");
        std::abort();
    }

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    vulkanFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    vulkanFunctions.vkAllocateMemory = vkAllocateMemory;
    vulkanFunctions.vkFreeMemory = vkFreeMemory;
    vulkanFunctions.vkMapMemory = vkMapMemory;
    vulkanFunctions.vkUnmapMemory = vkUnmapMemory;
    vulkanFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    vulkanFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
    vulkanFunctions.vkBindBufferMemory = vkBindBufferMemory;
    vulkanFunctions.vkBindImageMemory = vkBindImageMemory;
    vulkanFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
    vulkanFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    vulkanFunctions.vkCreateBuffer = vkCreateBuffer;
    vulkanFunctions.vkDestroyBuffer = vkDestroyBuffer;
    vulkanFunctions.vkCreateImage = vkCreateImage;
    vulkanFunctions.vkDestroyImage = vkDestroyImage;
    vulkanFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;
    vulkanFunctions.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
    vulkanFunctions.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
    vulkanFunctions.vkBindBufferMemory2KHR = vkBindBufferMemory2;
    vulkanFunctions.vkBindImageMemory2KHR = vkBindImageMemory2;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
    vulkanFunctions.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements;
    vulkanFunctions.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements;

    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    vmaCreateAllocator(&allocatorInfo, &allocator);

    deviceInfo.properties.pNext = &deviceInfo.descriptorBufferProps;
    deviceInfo.descriptorBufferProps.pNext = &deviceInfo.meshShaderProps;
    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceInfo.properties);

    SPDLOG_INFO("=== Vulkan Context Initialized ===");
    SPDLOG_INFO("GPU: {}", deviceInfo.properties.properties.deviceName);
    SPDLOG_INFO("Vulkan API: {}.{}.{}",
                VK_VERSION_MAJOR(deviceInfo.properties.properties.apiVersion),
                VK_VERSION_MINOR(deviceInfo.properties.properties.apiVersion),
                VK_VERSION_PATCH(deviceInfo.properties.properties.apiVersion));
    SPDLOG_INFO("Driver: {}.{}.{}",
                VK_VERSION_MAJOR(deviceInfo.properties.properties.driverVersion),
                VK_VERSION_MINOR(deviceInfo.properties.properties.driverVersion),
                VK_VERSION_PATCH(deviceInfo.properties.properties.driverVersion));
    SPDLOG_INFO("Queue Families - Graphics: {} | Transfer: {}", graphicsQueueFamily, transferQueueFamily);
    SPDLOG_INFO("Max Descriptor Buffer Bindings: {}", deviceInfo.descriptorBufferProps.maxDescriptorBufferBindings);
    SPDLOG_INFO("Mesh Shader Support - Max Task Workgroups: {}", deviceInfo.meshShaderProps.maxTaskWorkGroupCount[0]);
}

VulkanContext::~VulkanContext()
{
    vmaDestroyAllocator(allocator);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    vkb::destroy_debug_utils_messenger(instance, debugMessenger);
    vkDestroyInstance(instance, nullptr);
}
} // Renderer
