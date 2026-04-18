//
// Created by William on 2025-12-11.
//

#include "vk_context.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <SDL3/SDL.h>
#include "core/memory/memory_manager.h"
#include "core/containers/array.h"
#include "core/containers/inline_vector.h"
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vk_enum_string_helper.h>

#include "engine/logging/engine_log.h"

namespace Render
{
DeviceInfo VulkanContext::deviceInfo{};

// If ever considering a custom allocator for vulkan types, then use these with a mutex
static void* VKAPI_PTR VkAllocate(void* pUserData, size_t size, size_t, VkSystemAllocationScope)
{
    return static_cast<Core::MemoryManager*>(pUserData)->RenderAllocRaw(size);
}

static void* VKAPI_PTR VkReallocate(void* pUserData, void* pOriginal, size_t size, size_t, VkSystemAllocationScope)
{
    return static_cast<Core::MemoryManager*>(pUserData)->RenderRealloc(pOriginal, size);
}

static void VKAPI_PTR VkFree(void* pUserData, void* pMemory)
{
    static_cast<Core::MemoryManager*>(pUserData)->RenderFree(pMemory);
}

#ifndef PACKAGED_BUILD
static void VKAPI_PTR VmaDeviceAllocate(VmaAllocator, uint32_t, VkDeviceMemory, VkDeviceSize size, void* pUserData)
{
    static_cast<Core::MemoryManager*>(pUserData)->TrackDeviceAlloc(size);
}

static void VKAPI_PTR VmaDeviceFree(VmaAllocator, uint32_t, VkDeviceMemory, VkDeviceSize size, void* pUserData)
{
    static_cast<Core::MemoryManager*>(pUserData)->TrackDeviceFree(size);
}
#endif

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOG_ERROR(Engine, "[Vulkan] {}", pCallbackData->pMessage);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_ERROR(Engine, "[Vulkan] {}", pCallbackData->pMessage);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        LOG_ERROR(Engine, "[Vulkan] {}", pCallbackData->pMessage);
    }
    else {
        LOG_ERROR(Engine, "[Vulkan] {}", pCallbackData->pMessage);
    }

#ifdef _DEBUG
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        __debugbreak();
    }
#endif

    return VK_FALSE;
}

VulkanContext::VulkanContext(SDL_Window* window, Core::MemoryManager& memoryManager)
{
    VkResult res = volkInitialize();
    if (res != VK_SUCCESS) {
        LOG_ERROR(Engine, "Failed to initialize volk: {}", string_VkResult(res));
        std::abort();
    }

    // --- Instance ---
    {
        bool bUseValidation = false;
#ifdef ENABLE_VULKAN_VALIDATION
        bUseValidation = true;
#endif

        uint32_t sdlExtCount = 0;
        const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

        Core::InlineVector<const char*, 32> instanceExtensions;
        for (uint32_t i = 0; i < sdlExtCount; i++) {
            instanceExtensions.PushBack(sdlExtensions[i]);
        }
        if (bUseValidation) {
            instanceExtensions.PushBack(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        Core::InlineVector<const char*, 4> instanceLayers;
        if (bUseValidation) {
            instanceLayers.PushBack("VK_LAYER_KHRONOS_validation");
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Will Engine";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Will Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_4;

        Core::Array<VkValidationFeatureEnableEXT, 2> validationEnables = {
            VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        };
        VkValidationFeaturesEXT validationFeatures{};
        validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validationFeatures.enabledValidationFeatureCount = static_cast<uint32_t>(validationEnables.Size());
        validationFeatures.pEnabledValidationFeatures = validationEnables.Data();

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pNext = bUseValidation ? &validationFeatures : nullptr;
        instanceInfo.pApplicationInfo = &appInfo;
        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.Size());
        instanceInfo.ppEnabledExtensionNames = instanceExtensions.Data();
        instanceInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.Size());
        instanceInfo.ppEnabledLayerNames = instanceLayers.Data();

        res = vkCreateInstance(&instanceInfo, nullptr, &instance);
        if (res != VK_SUCCESS) {
            LOG_ERROR(Engine, "Failed to create Vulkan instance: {}", string_VkResult(res));
            std::abort();
        }
        volkLoadInstanceOnly(instance);

        if (bUseValidation) {
            VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
            debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debugInfo.messageSeverity =
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugInfo.messageType =
                    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugInfo.pfnUserCallback = VulkanDebugCallback;

            res = vkCreateDebugUtilsMessengerEXT(instance, &debugInfo, nullptr, &debugMessenger);
            if (res != VK_SUCCESS) {
                LOG_ERROR(Engine, "Failed to create debug messenger: {}", string_VkResult(res));
                std::abort();
            }
        }
    }

    SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface);

    // Feature structs declared here so they remain in scope for vkCreateDevice below
    VkPhysicalDeviceVulkan13Features features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features features12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features features11{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceFeatures features10{};
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT};
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceMaintenance9FeaturesKHR maintenance9Features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR};

    // --- Physical Device Selection ---
    {
        Core::InlineVector<const char*, 8> requiredDeviceExtensions;
        requiredDeviceExtensions.PushBack(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        requiredDeviceExtensions.PushBack(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
        requiredDeviceExtensions.PushBack(VK_EXT_MESH_SHADER_EXTENSION_NAME);
#if PROFILER_ENABLED
        requiredDeviceExtensions.PushBack(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
#endif

        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            LOG_ERROR(Engine, "No Vulkan-capable GPUs found");
            std::abort();
        }
        assert(deviceCount <= 8);
        Core::Array<VkPhysicalDevice, 8> pdCandidates;
        vkEnumeratePhysicalDevices(instance, &deviceCount, pdCandidates.Data());

        VkPhysicalDevice selected = VK_NULL_HANDLE;
        bool selectedIsDiscrete = false;
        bool selectedMaintenance9 = false;
        uint32_t selectedGraphicsFamily = UINT32_MAX;
        uint32_t selectedTransferFamily = UINT32_MAX;

        for (uint32_t i = 0; i < deviceCount; i++) {
            VkPhysicalDevice pd = pdCandidates[i];

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(pd, &props);
            if (props.apiVersion < VK_API_VERSION_1_3) {
                continue;
            }

            // Check required extensions
            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, nullptr);
            assert(extCount <= 512);
            Core::Array<VkExtensionProperties, 512> exts;
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, exts.Data());

            bool hasAllExts = true;
            for (uint32_t r = 0; r < requiredDeviceExtensions.Size(); r++) {
                bool found = false;
                for (uint32_t j = 0; j < extCount; j++) {
                    if (strcmp(exts[j].extensionName, requiredDeviceExtensions[r]) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    hasAllExts = false;
                    break;
                }
            }
            if (!hasAllExts) {
                continue;
            }

            // Check required features
            VkPhysicalDeviceFeatures2 query{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            VkPhysicalDeviceVulkan13Features q13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceVulkan12Features q12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            VkPhysicalDeviceVulkan11Features q11{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
            VkPhysicalDeviceDescriptorBufferFeaturesEXT qDesc{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT};
            VkPhysicalDeviceMeshShaderFeaturesEXT qMesh{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};

            query.pNext = &q13;
            q13.pNext = &q12;
            q12.pNext = &q11;
            q11.pNext = &qDesc;
            qDesc.pNext = &qMesh;

            vkGetPhysicalDeviceFeatures2(pd, &query);

            bool hasFeatures =
                    // Modern Rendering (Vulkan 1.3)
                    q13.dynamicRendering &&
                    q13.synchronization2 &&
                    // GPU Driven Rendering (Vulkan 1.2)
                    q12.bufferDeviceAddress &&
                    q12.runtimeDescriptorArray &&
                    q12.shaderSampledImageArrayNonUniformIndexing &&
                    q12.shaderStorageImageArrayNonUniformIndexing &&
                    q12.shaderUniformBufferArrayNonUniformIndexing &&
                    q12.shaderStorageBufferArrayNonUniformIndexing &&
                    q12.drawIndirectCount &&
                    // Shader types (Vulkan 1.2)
                    q12.shaderInt8 &&
                    q12.shaderFloat16 &&
#if PROFILER_ENABLED
                    q12.hostQueryReset &&
#endif
                    // SV_VertexID (Vulkan 1.1)
                    q11.shaderDrawParameters &&
                    // GPU Driven Rendering (core)
                    query.features.multiDrawIndirect &&
                    // Shader types (core)
                    query.features.shaderInt16 &&
                    query.features.shaderInt64 &&
                    // Gather / clip
                    query.features.shaderImageGatherExtended &&
                    query.features.shaderClipDistance &&
                    // Extensions
                    qDesc.descriptorBuffer &&
                    qMesh.taskShader &&
                    qMesh.meshShader;

            if (!hasFeatures) {
                continue;
            }

            // Find queue families
            uint32_t qfCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, nullptr);
            assert(qfCount <= 16);
            Core::Array<VkQueueFamilyProperties, 16> queueFamilyProperties;
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, queueFamilyProperties.Data());

            uint32_t graphicsFamily = UINT32_MAX;
            uint32_t transferFamily = UINT32_MAX;

            for (uint32_t j = 0; j < qfCount; j++) {
                if ((queueFamilyProperties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) && graphicsFamily == UINT32_MAX) {
                    VkBool32 presentSupport = VK_FALSE;
                    vkGetPhysicalDeviceSurfaceSupportKHR(pd, j, surface, &presentSupport);
                    if (presentSupport) {
                        graphicsFamily = j;
                    }
                }
            }

            for (uint32_t j = 0; j < qfCount; j++) {
                if (j == graphicsFamily) {
                    continue;
                }
                if ((queueFamilyProperties[j].queueFlags & VK_QUEUE_TRANSFER_BIT) && !(queueFamilyProperties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    transferFamily = j;
                    break;
                }
            }

            if (graphicsFamily == UINT32_MAX || transferFamily == UINT32_MAX) {
                continue;
            }

            bool hasMaintenance9 = false;
            for (uint32_t j = 0; j < extCount; j++) {
                if (strcmp(exts[j].extensionName, VK_KHR_MAINTENANCE_9_EXTENSION_NAME) == 0) {
                    hasMaintenance9 = true;
                    break;
                }
            }

            bool isDiscrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
            if (selected == VK_NULL_HANDLE || (isDiscrete && !selectedIsDiscrete)) {
                selected = pd;
                selectedGraphicsFamily = graphicsFamily;
                selectedTransferFamily = transferFamily;
                selectedMaintenance9 = hasMaintenance9;
                selectedIsDiscrete = isDiscrete;
            }
        }

        if (selected == VK_NULL_HANDLE) {
            LOG_ERROR(Engine, "No suitable GPU found; requires Vulkan 1.3, mesh shaders, and descriptor buffers");
            std::abort();
        }

        physicalDevice = selected;
        graphicsQueueFamily = selectedGraphicsFamily;
        transferQueueFamily = selectedTransferFamily;
        bMaintenance9Enabled = selectedMaintenance9;
    }

    // --- Logical Device ---
    {
        // Modern Rendering (Vulkan 1.3)
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;

        // GPU Driven Rendering (Vulkan 1.2)
        features12.bufferDeviceAddress = VK_TRUE;
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
        features12.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
        features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        features12.drawIndirectCount = VK_TRUE;

        // Shader types (Vulkan 1.2)
        features12.shaderInt8 = VK_TRUE;
        features12.shaderFloat16 = VK_TRUE;
#if PROFILER_ENABLED
        features12.hostQueryReset = VK_TRUE;
#endif

        // SV_VertexID (Vulkan 1.1)
        features11.shaderDrawParameters = VK_TRUE;

        // GPU Driven Rendering (core)
        features10.multiDrawIndirect = VK_TRUE;

        // Shader types (core)
        features10.shaderInt16 = VK_TRUE;
        features10.shaderInt64 = VK_TRUE;

        // Gather / clip
        features10.shaderImageGatherExtended = VK_TRUE;
        features10.shaderClipDistance = VK_TRUE;
#ifdef ENABLE_VULKAN_VALIDATION
        // Suppresses a false-positive validation error: SV_PrimitiveID in mesh shaders
        // triggers a geometry shader requirement check that doesn't apply here.
        features10.geometryShader = VK_TRUE;
#endif

        // Extensions
        descriptorBufferFeatures.descriptorBuffer = VK_TRUE;
        meshShaderFeatures.taskShader = VK_TRUE;
        meshShaderFeatures.meshShader = VK_TRUE;
        if (bMaintenance9Enabled) {
            maintenance9Features.maintenance9 = VK_TRUE;
        }

        features13.pNext = &features12;
        features12.pNext = &features11;
        features11.pNext = &descriptorBufferFeatures;
        descriptorBufferFeatures.pNext = &meshShaderFeatures;
        meshShaderFeatures.pNext = bMaintenance9Enabled ? static_cast<void*>(&maintenance9Features) : nullptr;

        Core::InlineVector<const char*, 8> deviceExts;
        deviceExts.PushBack(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        deviceExts.PushBack(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
        deviceExts.PushBack(VK_EXT_MESH_SHADER_EXTENSION_NAME);
#if PROFILER_ENABLED
        deviceExts.PushBack(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
#endif
        if (bMaintenance9Enabled) {
            deviceExts.PushBack(VK_KHR_MAINTENANCE_9_EXTENSION_NAME);
        }

        float queuePriority = 1.0f;
        Core::Array<VkDeviceQueueCreateInfo, 2> queueInfos = {
            VkDeviceQueueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, graphicsQueueFamily, 1, &queuePriority},
            VkDeviceQueueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, transferQueueFamily, 1, &queuePriority},
        };

        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pNext = &features13;
        deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.Size());
        deviceCreateInfo.pQueueCreateInfos = queueInfos.Data();
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExts.Size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExts.Data();
        deviceCreateInfo.pEnabledFeatures = &features10;

        res = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
        if (res != VK_SUCCESS) {
            LOG_ERROR(Engine, "Failed to create Vulkan device: {}", string_VkResult(res));
            std::abort();
        }
        volkLoadDevice(device);
    }

    vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, transferQueueFamily, 0, &transferQueue);

    if (graphicsQueueFamily == transferQueueFamily) {
        LOG_ERROR(Engine, "Graphics and transfer queue families are the same ({})", graphicsQueueFamily);
        LOG_ERROR(Engine, "Your system may not support Vulkan");
        std::abort();
    }

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

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
    // todo: idk about this
    // allocatorInfo.pAllocationCallbacks = &allocationCallbacks;
#ifndef PACKAGED_BUILD
    VmaDeviceMemoryCallbacks deviceMemoryCallbacks{};
    deviceMemoryCallbacks.pfnAllocate = VmaDeviceAllocate;
    deviceMemoryCallbacks.pfnFree = VmaDeviceFree;
    deviceMemoryCallbacks.pUserData = &memoryManager;
    allocatorInfo.pDeviceMemoryCallbacks = &deviceMemoryCallbacks;
#endif
    vmaCreateAllocator(&allocatorInfo, &allocator);

    deviceInfo.properties.pNext = &deviceInfo.descriptorBufferProps;
    deviceInfo.descriptorBufferProps.pNext = &deviceInfo.meshShaderProps;
    deviceInfo.meshShaderProps.pNext = &deviceInfo.subgroupProps;
    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceInfo.properties);

#if PROFILER_ENABLED
    tracyContext = TracyVkContextHostCalibrated(
        physicalDevice, device,
        vkResetQueryPool,
        vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
        vkGetCalibratedTimestampsEXT
    );
    TracyVkContextName(tracyContext, "Graphics", 8);
#endif

    LOG_INFO(Engine, "=== Vulkan Context Initialized ===");
    LOG_INFO(Engine, "GPU: {}", deviceInfo.properties.properties.deviceName);
    LOG_INFO(Engine, "Vulkan API: {}.{}.{}",
             VK_VERSION_MAJOR(deviceInfo.properties.properties.apiVersion),
             VK_VERSION_MINOR(deviceInfo.properties.properties.apiVersion),
             VK_VERSION_PATCH(deviceInfo.properties.properties.apiVersion));
    LOG_INFO(Engine, "Driver: {}.{}.{}",
             VK_VERSION_MAJOR(deviceInfo.properties.properties.driverVersion),
             VK_VERSION_MINOR(deviceInfo.properties.properties.driverVersion),
             VK_VERSION_PATCH(deviceInfo.properties.properties.driverVersion));
    LOG_INFO(Engine, "Queue Families - Graphics: {} | Transfer: {}", graphicsQueueFamily, transferQueueFamily);
    LOG_INFO(Engine, "Max Push Constant Size: {}", deviceInfo.properties.properties.limits.maxPushConstantsSize);
    LOG_INFO(Engine, "Max Descriptor Buffer Bindings: {}", deviceInfo.descriptorBufferProps.maxDescriptorBufferBindings);
    LOG_INFO(Engine, "Mesh Shader Support - Max Task Workgroups: {}", deviceInfo.meshShaderProps.maxTaskWorkGroupCount[0]);
    LOG_INFO(Engine, "Mesh Shader Support - Max Task Workgroups: {}", deviceInfo.meshShaderProps.maxTaskWorkGroupCount[0]);
    LOG_INFO(Engine, "Mesh Shader - Max Task Work Group Count: [{}, {}, {}]",
             deviceInfo.meshShaderProps.maxTaskWorkGroupCount[0],
             deviceInfo.meshShaderProps.maxTaskWorkGroupCount[1],
             deviceInfo.meshShaderProps.maxTaskWorkGroupCount[2]);
    LOG_INFO(Engine, "Mesh Shader - Max Mesh Work Group Count: [{}, {}, {}]",
             deviceInfo.meshShaderProps.maxMeshWorkGroupCount[0],
             deviceInfo.meshShaderProps.maxMeshWorkGroupCount[1],
             deviceInfo.meshShaderProps.maxMeshWorkGroupCount[2]);
    LOG_INFO(Engine, "Max Draw Indirect Count: {}", deviceInfo.properties.properties.limits.maxDrawIndirectCount);
    LOG_INFO(Engine, "Subgroup Size: {}", deviceInfo.subgroupProps.subgroupSize);
    if (deviceInfo.subgroupProps.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) {
        LOG_INFO(Engine, "Subgroup Arithmetic operations supported (WavePrefixSum)");
    }
}

VulkanContext::~VulkanContext()
{
    TracyVkDestroy(tracyContext);

    vmaDestroyAllocator(allocator);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    if (debugMessenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }
    vkDestroyInstance(instance, nullptr);
}
} // Renderer
