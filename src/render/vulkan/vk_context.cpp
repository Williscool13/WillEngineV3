//
// Created by William on 2025-12-11.
//

#include "vk_context.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <SDL3/SDL.h>
#include "core/memory/memory_manager.h"
#include "core/containers/array.h"
#include "core/containers/inline_vector.h"
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vk_enum_string_helper.h>

#include "engine/logging/engine_log.h"
#include "render/vulkan/vk_utils.h"

namespace Render
{
DeviceInfo VulkanContext::deviceInfo{};
bool VulkanContext::bForceNoREBAR{false};
bool VulkanContext::bEnableSyncValidation{false};

static VkDevice gFaultReportDevice{VK_NULL_HANDLE};

#ifdef WDEBUG
struct AddressBindingEvent
{
    VkDeviceAddress base;
    VkDeviceSize size;
    uint64_t handle;
    uint64_t tickMs;
    VkObjectType objectType;
    VkDeviceAddressBindingTypeEXT bindingType;
    char name[48];
};

static constexpr uint32_t ADDRESS_BINDING_CAPACITY = 1 << 16;
static Core::Array<AddressBindingEvent, ADDRESS_BINDING_CAPACITY> gAddressBindings;
static std::atomic<uint32_t> gAddressBindingCount{0};

static VKAPI_ATTR VkBool32 VKAPI_CALL AddressBindingCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*)
{
    for (const auto* next = static_cast<const VkBaseInStructure*>(pCallbackData->pNext); next != nullptr; next = next->pNext) {
        if (next->sType != VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT) { continue; }

        const auto* binding = reinterpret_cast<const VkDeviceAddressBindingCallbackDataEXT*>(next);
        AddressBindingEvent& event = gAddressBindings[gAddressBindingCount.fetch_add(1, std::memory_order_relaxed) % ADDRESS_BINDING_CAPACITY];
        event.base = binding->baseAddress;
        event.size = binding->size;
        event.bindingType = binding->bindingType;
        event.tickMs = SDL_GetTicks();
        event.handle = pCallbackData->objectCount > 0 ? pCallbackData->pObjects[0].objectHandle : 0;
        event.objectType = pCallbackData->objectCount > 0 ? pCallbackData->pObjects[0].objectType : VK_OBJECT_TYPE_UNKNOWN;
        const char* objectName = pCallbackData->objectCount > 0 ? pCallbackData->pObjects[0].pObjectName : nullptr;
        strncpy_s(event.name, sizeof(event.name), objectName ? objectName : "", _TRUNCATE);
        break;
    }
    return VK_FALSE;
}

static constexpr uint32_t GPU_MARKER_FRAME_HISTORY = 16;
static constexpr uint32_t GPU_MARKER_MAX_PASSES = 1024;
static VkBuffer gGpuMarkerBuffer{VK_NULL_HANDLE};
static VmaAllocation gGpuMarkerAllocation{VK_NULL_HANDLE};
static volatile uint32_t* gGpuMarkerValues{nullptr};
static uint32_t gGpuMarkerFrame{0};
static Core::Array<Core::Array<const char*, GPU_MARKER_MAX_PASSES>, GPU_MARKER_FRAME_HISTORY> gGpuMarkerPassNames;

void BeginGpuMarkerFrame()
{
    gGpuMarkerFrame = (gGpuMarkerFrame + 1) & 0xFFFF;
}

void WriteGpuPassMarker(VkCommandBuffer cmd, bool bAsync, bool bEnd, uint32_t passIndex, const char* passName)
{
    if (gGpuMarkerBuffer == VK_NULL_HANDLE || passIndex >= GPU_MARKER_MAX_PASSES) { return; }
    gGpuMarkerPassNames[gGpuMarkerFrame % GPU_MARKER_FRAME_HISTORY][passIndex] = passName;
    const VkPipelineStageFlagBits stage = bEnd ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    const VkDeviceSize offset = ((bAsync ? 2u : 0u) + (bEnd ? 1u : 0u)) * sizeof(uint32_t);
    vkCmdWriteBufferMarkerAMD(cmd, stage, gGpuMarkerBuffer, offset, gGpuMarkerFrame << 16 | passIndex);
}

static void LogGpuMarker(const char* label, uint32_t value)
{
    if (value == 0) {
        LOG_ERROR(Engine, "Device fault marker {}: none written", label);
        return;
    }
    const uint32_t frame = value >> 16;
    const uint32_t passIndex = value & 0xFFFF;
    const char* name = gGpuMarkerPassNames[frame % GPU_MARKER_FRAME_HISTORY][passIndex];
    LOG_ERROR(Engine, "Device fault marker {}: frame {} pass {} '{}' (latest recorded frame {})", label, frame, passIndex, name ? name : "?", gGpuMarkerFrame);
}

static void ReportGpuMarkers()
{
    if (gGpuMarkerValues == nullptr) {
        LOG_ERROR(Engine, "Device fault markers: VK_AMD_buffer_marker is not enabled");
        return;
    }
    LogGpuMarker("graphics started", gGpuMarkerValues[0]);
    LogGpuMarker("graphics finished", gGpuMarkerValues[1]);
    LogGpuMarker("async started", gGpuMarkerValues[2]);
    LogGpuMarker("async finished", gGpuMarkerValues[3]);

    const uint32_t started = gGpuMarkerValues[0];
    const uint32_t finished = gGpuMarkerValues[1];
    if (started >> 16 == finished >> 16) {
        const uint32_t frame = started >> 16;
        for (uint32_t pass = (finished & 0xFFFF) + 1; pass <= (started & 0xFFFF) + 2 && pass < GPU_MARKER_MAX_PASSES; ++pass) {
            const char* name = gGpuMarkerPassNames[frame % GPU_MARKER_FRAME_HISTORY][pass];
            LOG_ERROR(Engine, "Device fault marker graphics candidate: pass {} '{}'", pass, name ? name : "?");
        }
    }
}

static void LogAddressBinding(const char* label, const AddressBindingEvent& event, uint64_t nowMs)
{
    LOG_ERROR(Engine, "Device fault history {}: {} {} 0x{:x} '{}' [0x{:x}, 0x{:x}) {} ms ago", label,
              event.bindingType == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT ? "bind" : "unbind", string_VkObjectType(event.objectType),
              event.handle, event.name, event.base, event.base + event.size, nowMs - event.tickMs);
}

static void ReportAddressHistory(VkDeviceAddress address, VkDeviceSize precision)
{
    constexpr uint32_t MAX_LOGGED = 12;
    const VkDeviceAddress pageBegin = address & ~(precision - 1);
    const VkDeviceAddress pageEnd = pageBegin + precision;
    const uint32_t total = gAddressBindingCount.load(std::memory_order_relaxed);
    const uint32_t recorded = std::min(total, ADDRESS_BINDING_CAPACITY);
    const uint64_t nowMs = SDL_GetTicks();
    LOG_ERROR(Engine, "Device fault history: {} binding events seen, searching newest {}", total, recorded);

    uint32_t logged = 0;
    const AddressBindingEvent* below = nullptr;
    const AddressBindingEvent* above = nullptr;
    for (uint32_t i = 0; i < recorded; ++i) {
        const AddressBindingEvent& event = gAddressBindings[(total - 1 - i) % ADDRESS_BINDING_CAPACITY];
        if (event.base < pageEnd && pageBegin < event.base + event.size) {
            if (logged < MAX_LOGGED) { LogAddressBinding("overlap", event, nowMs); }
            ++logged;
            continue;
        }
        if (event.base + event.size <= pageBegin && (below == nullptr || event.base + event.size > below->base + below->size)) { below = &event; }
        if (event.base >= pageEnd && (above == nullptr || event.base < above->base)) { above = &event; }
    }
    LOG_ERROR(Engine, "Device fault history: {} event(s) overlap the faulting page", logged);
    if (logged == 0) {
        if (below != nullptr) { LogAddressBinding("nearest below", *below, nowMs); }
        if (above != nullptr) { LogAddressBinding("nearest above", *above, nowMs); }
    }
}
#endif

void ReportDeviceFault()
{
    static std::once_flag reportOnce;
    std::call_once(reportOnce, [] {
        if (gFaultReportDevice == VK_NULL_HANDLE) {
            LOG_ERROR(Engine, "Device fault: VK_EXT_device_fault is not enabled");
            return;
        }

        constexpr uint32_t MAX_ADDRESS_INFOS = 32;
        constexpr uint32_t MAX_VENDOR_INFOS = 32;
        VkDeviceFaultCountsEXT counts{.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
        VkResult res = vkGetDeviceFaultInfoEXT(gFaultReportDevice, &counts, nullptr);
        if (res != VK_SUCCESS) {
            LOG_ERROR(Engine, "Device fault: count query failed: {}", string_VkResult(res));
            return;
        }
        LOG_ERROR(Engine, "Device fault: {} address info(s), {} vendor info(s), {} vendor binary bytes", counts.addressInfoCount, counts.vendorInfoCount, counts.vendorBinarySize);

        Core::Array<VkDeviceFaultAddressInfoEXT, MAX_ADDRESS_INFOS> addressInfos{};
        Core::Array<VkDeviceFaultVendorInfoEXT, MAX_VENDOR_INFOS> vendorInfos{};
        counts.addressInfoCount = std::min(counts.addressInfoCount, MAX_ADDRESS_INFOS);
        counts.vendorInfoCount = std::min(counts.vendorInfoCount, MAX_VENDOR_INFOS);
        counts.vendorBinarySize = 0;

        VkDeviceFaultInfoEXT info{.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT};
        info.pAddressInfos = addressInfos.Data();
        info.pVendorInfos = vendorInfos.Data();
        res = vkGetDeviceFaultInfoEXT(gFaultReportDevice, &counts, &info);
        if (res != VK_SUCCESS && res != VK_INCOMPLETE) {
            LOG_ERROR(Engine, "Device fault: info query failed: {}", string_VkResult(res));
            return;
        }

        LOG_ERROR(Engine, "Device fault: {}", info.description);
        for (uint32_t i = 0; i < counts.addressInfoCount; ++i) {
            const VkDeviceFaultAddressInfoEXT& address = addressInfos[i];
            LOG_ERROR(Engine, "Device fault address {}: {} at 0x{:x} (precision 0x{:x})", i, string_VkDeviceFaultAddressTypeEXT(address.addressType), address.reportedAddress, address.addressPrecision);
#ifdef WDEBUG
            ReportAddressHistory(address.reportedAddress, address.addressPrecision);
#endif
        }
        for (uint32_t i = 0; i < counts.vendorInfoCount; ++i) {
            const VkDeviceFaultVendorInfoEXT& vendor = vendorInfos[i];
            LOG_ERROR(Engine, "Device fault vendor {}: {} (code 0x{:x}, data 0x{:x})", i, vendor.description, vendor.vendorFaultCode, vendor.vendorFaultData);
        }
#ifdef WDEBUG
        ReportGpuMarkers();
#endif
    });
}

static void* VKAPI_PTR VkHostAlloc(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope)
{
    if (size == 0) { return nullptr; }
    return static_cast<Core::MemoryManager*>(pUserData)->Vulkan().AlignedAlloc(size, alignment, Core::AllocTag::Vulkan);
}

static void* VKAPI_PTR VkHostRealloc(void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope)
{
    return static_cast<Core::MemoryManager*>(pUserData)->Vulkan().AlignedRealloc(pOriginal, size, alignment, Core::AllocTag::Vulkan);
}

static void VKAPI_PTR VkHostFree(void* pUserData, void* pMemory)
{
    static_cast<Core::MemoryManager*>(pUserData)->Vulkan().AlignedFree(pMemory);
}

#ifdef WDEBUG
static void VKAPI_PTR VmaDeviceAllocate(VmaAllocator, uint32_t, VkDeviceMemory, VkDeviceSize size, void* pUserData)
{
    static_cast<Core::MemoryManager*>(pUserData)->TrackDeviceAlloc(size);
}

static void VKAPI_PTR VmaDeviceFree(VmaAllocator, uint32_t, VkDeviceMemory, VkDeviceSize size, void* pUserData)
{
    static_cast<Core::MemoryManager*>(pUserData)->TrackDeviceFree(size);
}
#endif

#ifdef WDEBUG
struct GeometryLoadRecord
{
    VkCommandBuffer transferCmd;
    VkCommandBuffer graphicsCmd;
    VkDeviceSize positionBegin;
    VkDeviceSize positionEnd;
    VkDeviceSize indexBegin;
    VkDeviceSize indexEnd;
    bool bInFlight;
    char model[64];
};

static constexpr uint32_t GEOMETRY_LOAD_RECORD_COUNT = 1024;
static Core::Array<GeometryLoadRecord, GEOMETRY_LOAD_RECORD_COUNT> gGeometryLoads;
static uint32_t gGeometryLoadCount{0};
static std::mutex gGeometryLoadMutex;

static bool RangesOverlap(VkDeviceSize aBegin, VkDeviceSize aEnd, VkDeviceSize bBegin, VkDeviceSize bEnd)
{
    return aBegin < bEnd && bBegin < aEnd;
}

void TrackGeometryLoadBegin(VkCommandBuffer transferCmd, VkCommandBuffer graphicsCmd, VkDeviceSize positionBegin, VkDeviceSize positionSize, VkDeviceSize indexBegin, VkDeviceSize indexSize, const char* model)
{
    std::lock_guard lock(gGeometryLoadMutex);
    const uint32_t recorded = std::min(gGeometryLoadCount, GEOMETRY_LOAD_RECORD_COUNT);
    for (uint32_t i = 0; i < recorded; ++i) {
        const GeometryLoadRecord& other = gGeometryLoads[i];
        if (!other.bInFlight) { continue; }
        const bool bPosition = RangesOverlap(positionBegin, positionBegin + positionSize, other.positionBegin, other.positionEnd);
        const bool bIndex = RangesOverlap(indexBegin, indexBegin + indexSize, other.indexBegin, other.indexEnd);
        if (bPosition || bIndex) {
            LOG_ERROR(Engine, "Geometry loads overlap: '{}' and in-flight '{}' share bytes (position {}, index {})", model, other.model, bPosition, bIndex);
            __debugbreak();
        }
    }
    GeometryLoadRecord& record = gGeometryLoads[gGeometryLoadCount++ % GEOMETRY_LOAD_RECORD_COUNT];
    record = {transferCmd, graphicsCmd, positionBegin, positionBegin + positionSize, indexBegin, indexBegin + indexSize, true, {}};
    strncpy_s(record.model, sizeof(record.model), model, _TRUNCATE);
}

void TrackGeometryLoadEnd(VkCommandBuffer transferCmd)
{
    std::lock_guard lock(gGeometryLoadMutex);
    const uint32_t recorded = std::min(gGeometryLoadCount, GEOMETRY_LOAD_RECORD_COUNT);
    for (uint32_t i = 0; i < recorded; ++i) {
        GeometryLoadRecord& record = gGeometryLoads[(gGeometryLoadCount - 1 - i) % GEOMETRY_LOAD_RECORD_COUNT];
        if (record.bInFlight && record.transferCmd == transferCmd) {
            record.bInFlight = false;
            return;
        }
    }
}

static const GeometryLoadRecord* FindGeometryLoad(uint64_t cmd)
{
    const uint32_t recorded = std::min(gGeometryLoadCount, GEOMETRY_LOAD_RECORD_COUNT);
    for (uint32_t i = 0; i < recorded; ++i) {
        const GeometryLoadRecord& record = gGeometryLoads[(gGeometryLoadCount - 1 - i) % GEOMETRY_LOAD_RECORD_COUNT];
        if (reinterpret_cast<uint64_t>(record.transferCmd) == cmd || reinterpret_cast<uint64_t>(record.graphicsCmd) == cmd) { return &record; }
    }
    return nullptr;
}

enum class GeometryHazard : uint8_t
{
    NotGeometry,
    Disjoint,
    Overlap,
    Unmatched,
};

/** A BLAS build declares the vertex buffer from its base to the model's last vertex because indices are global, so syncval sees it overlap copies to lower models. Compare the bytes each one actually uses. */
static GeometryHazard ClassifyGeometryHazard(const char* message)
{
    if (message == nullptr) { return GeometryHazard::NotGeometry; }
    const bool bBuildReadsCopy = strstr(message, "READ_RACING_WRITE") != nullptr && strstr(message, "vkCmdBuildAccelerationStructuresKHR") != nullptr;
    const bool bCopyWritesBuild = strstr(message, "WRITE_RACING_READ") != nullptr && strstr(message, "vkCmdCopyBuffer") != nullptr;
    if (!bBuildReadsCopy && !bCopyWritesBuild) { return GeometryHazard::NotGeometry; }

    constexpr const char* CMD_PREFIX = "VkCommandBuffer 0x";
    const char* first = strstr(message, CMD_PREFIX);
    const char* second = first ? strstr(first + 1, CMD_PREFIX) : nullptr;
    const bool bPositionBuffer = strstr(message, "Mega Vertex Position") != nullptr;
    const bool bIndexBuffer = strstr(message, "Mega Index") != nullptr;
    if (first == nullptr || second == nullptr || (!bPositionBuffer && !bIndexBuffer)) { return GeometryHazard::Unmatched; }

    std::lock_guard lock(gGeometryLoadMutex);
    const GeometryLoadRecord* a = FindGeometryLoad(strtoull(first + strlen(CMD_PREFIX), nullptr, 16));
    const GeometryLoadRecord* b = FindGeometryLoad(strtoull(second + strlen(CMD_PREFIX), nullptr, 16));
    if (a == nullptr || b == nullptr) { return GeometryHazard::Unmatched; }

    const VkDeviceSize aBegin = bIndexBuffer ? a->indexBegin : a->positionBegin;
    const VkDeviceSize aEnd = bIndexBuffer ? a->indexEnd : a->positionEnd;
    const VkDeviceSize bBegin = bIndexBuffer ? b->indexBegin : b->positionBegin;
    const VkDeviceSize bEnd = bIndexBuffer ? b->indexEnd : b->positionEnd;
    if (!RangesOverlap(aBegin, aEnd, bBegin, bEnd)) { return GeometryHazard::Disjoint; }

    LOG_ERROR(Engine, "Geometry hazard overlaps: '{}' [{}, {}) and '{}' [{}, {})", a->model, aBegin, aEnd, b->model, bBegin, bEnd);
    return GeometryHazard::Overlap;
}
#endif

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if (pCallbackData->pMessage && strstr(pCallbackData->pMessage, "vkGetQueryPoolResults") != nullptr && strstr(pCallbackData->pMessage, "VK_NOT_READY") != nullptr) {
        return VK_FALSE;
    }

#ifdef WDEBUG
    const GeometryHazard hazard = ClassifyGeometryHazard(pCallbackData->pMessage);
    if (hazard == GeometryHazard::Disjoint) { return VK_FALSE; }
#endif

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

#ifdef WDEBUG
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) && hazard != GeometryHazard::Unmatched) {
        __debugbreak();
    }
#endif

    return VK_FALSE;
}

static bool DetectREBAR(const VkPhysicalDeviceMemoryProperties& memProps)
{
    uint32_t largestDeviceLocalHeap = UINT32_MAX;
    VkDeviceSize largestSize = 0;
    for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
        if ((memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) { continue; }
        if (memProps.memoryHeaps[i].size <= largestSize) { continue; }
        largestSize = memProps.memoryHeaps[i].size;
        largestDeviceLocalHeap = i;
    }
    if (largestDeviceLocalHeap == UINT32_MAX) { return false; }

    constexpr VkMemoryPropertyFlags REQUIRED = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memProps.memoryTypes[i].propertyFlags & REQUIRED) != REQUIRED) { continue; }
        if (memProps.memoryTypes[i].heapIndex == largestDeviceLocalHeap) { return true; }
    }
    return false;
}

VulkanContext::VulkanContext(SDL_Window* window, Core::MemoryManager& memoryManager)
{
    hostAllocationCallbacks.pUserData = &memoryManager;
    hostAllocationCallbacks.pfnAllocation = VkHostAlloc;
    hostAllocationCallbacks.pfnReallocation = VkHostRealloc;
    hostAllocationCallbacks.pfnFree = VkHostFree;

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
        bool bDebugUtils = bUseValidation;
#ifdef WDEBUG
        bDebugUtils = true;
#endif

        uint32_t sdlExtCount = 0;
        const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

        Core::InlineVector<const char*, 32> instanceExtensions;
        for (uint32_t i = 0; i < sdlExtCount; i++) {
            instanceExtensions.PushBack(sdlExtensions[i]);
        }
        if (bDebugUtils) {
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
            validationFeatures.enabledValidationFeatureCount = static_cast<uint32_t>(validationEnables.Size()) - (bEnableSyncValidation ? 0u : 1u);
        validationFeatures.pEnabledValidationFeatures = validationEnables.Data();

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pNext = bUseValidation ? &validationFeatures : nullptr;
        instanceInfo.pApplicationInfo = &appInfo;
        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.Size());
        instanceInfo.ppEnabledExtensionNames = instanceExtensions.Data();
        instanceInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.Size());
        instanceInfo.ppEnabledLayerNames = instanceLayers.Data();

        res = vkCreateInstance(&instanceInfo, &hostAllocationCallbacks, &instance);
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

            res = vkCreateDebugUtilsMessengerEXT(instance, &debugInfo, &hostAllocationCallbacks, &debugMessenger);
            if (res != VK_SUCCESS) {
                LOG_ERROR(Engine, "Failed to create debug messenger: {}", string_VkResult(res));
                std::abort();
            }
        }

#ifdef WDEBUG
        VkDebugUtilsMessengerCreateInfoEXT addressBindingInfo{.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        addressBindingInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        addressBindingInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;
        addressBindingInfo.pfnUserCallback = AddressBindingCallback;
        res = vkCreateDebugUtilsMessengerEXT(instance, &addressBindingInfo, &hostAllocationCallbacks, &addressBindingMessenger);
        if (res != VK_SUCCESS) {
            LOG_ERROR(Engine, "Failed to create address binding messenger: {}", string_VkResult(res));
        }
#endif
    }

    SDL_Vulkan_CreateSurface(window, instance, &hostAllocationCallbacks, &surface);

    // Feature structs declared here so they remain in scope for vkCreateDevice below
    VkPhysicalDeviceVulkan13Features features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features features12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features features11{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceFeatures features10{.independentBlend = VK_TRUE};
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT};
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT extendedDynamicState3Features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT};
    VkPhysicalDeviceMaintenance9FeaturesKHR maintenance9Features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceFaultFeaturesEXT deviceFaultFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
#ifdef WDEBUG
    VkPhysicalDeviceAddressBindingReportFeaturesEXT addressBindingFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT};
    bool bAddressBindingEnabled = false;
    bool bGpuMarkersEnabled = false;
#endif
#ifdef ENABLE_VULKAN_VALIDATION
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR pipelineExecutablePropertiesFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};
#endif

    uint32_t computeQueueIndex = 0;

    // --- Physical Device Selection ---
    {
        Core::InlineVector<const char*, 12> requiredDeviceExtensions;
        requiredDeviceExtensions.PushBack(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        requiredDeviceExtensions.PushBack(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
        requiredDeviceExtensions.PushBack(VK_EXT_MESH_SHADER_EXTENSION_NAME);
        requiredDeviceExtensions.PushBack(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        requiredDeviceExtensions.PushBack(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        requiredDeviceExtensions.PushBack(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        requiredDeviceExtensions.PushBack(VK_KHR_RAY_QUERY_EXTENSION_NAME);
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
        bool selectedMeshShaderQueries = false;
        bool selectedDeviceFault = false;
#ifdef ENABLE_VULKAN_VALIDATION
        bool selectedPipelineExecProps = false;
#endif
        uint32_t selectedGraphicsFamily = UINT32_MAX;
        uint32_t selectedTransferFamily = UINT32_MAX;
        uint32_t selectedComputeFamily = UINT32_MAX;
        uint32_t selectedComputeQueueIndex = 0;

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
            VkPhysicalDeviceExtendedDynamicState3FeaturesEXT qExtDynState3{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT};
            VkPhysicalDeviceAccelerationStructureFeaturesKHR qAccel{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
            VkPhysicalDeviceRayQueryFeaturesKHR qRayQuery{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

            query.pNext = &q13;
            q13.pNext = &q12;
            q12.pNext = &q11;
            q11.pNext = &qDesc;
            qDesc.pNext = &qMesh;
            qMesh.pNext = &qExtDynState3;
            qExtDynState3.pNext = &qAccel;
            qAccel.pNext = &qRayQuery;

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
                    q12.scalarBlockLayout &&
                    q12.timelineSemaphore &&
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
                    query.features.samplerAnisotropy &&
                    // Shadow map pancaking
                    query.features.depthClamp &&
                    // BC Compression
                    query.features.textureCompressionBC &&
                    // Extensions
                    qDesc.descriptorBuffer &&
                    qMesh.taskShader &&
                    qMesh.meshShader &&
                    qExtDynState3.extendedDynamicState3PolygonMode &&
                    qAccel.accelerationStructure &&
                    qRayQuery.rayQuery;

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

            // Prefer a transfer-only (DMA) family so compute-capable families stay free for the compute queue
            for (uint32_t j = 0; j < qfCount; j++) {
                if (j == graphicsFamily) {
                    continue;
                }
                const VkQueueFlags flags = queueFamilyProperties[j].queueFlags;
                if ((flags & VK_QUEUE_TRANSFER_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT) && !(flags & VK_QUEUE_COMPUTE_BIT)) {
                    transferFamily = j;
                    break;
                }
            }
            if (transferFamily == UINT32_MAX) {
                for (uint32_t j = 0; j < qfCount; j++) {
                    if (j == graphicsFamily) {
                        continue;
                    }
                    if ((queueFamilyProperties[j].queueFlags & VK_QUEUE_TRANSFER_BIT) && !(queueFamilyProperties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                        transferFamily = j;
                        break;
                    }
                }
            }

            uint32_t computeFamily = UINT32_MAX;
            uint32_t computeQueueIndexForFamily = 0;
            for (uint32_t j = 0; j < qfCount; j++) {
                if (j == graphicsFamily || j == transferFamily) {
                    continue;
                }
                if (queueFamilyProperties[j].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    computeFamily = j;
                    break;
                }
            }

            // Transfer landed on a compute-capable family (no DMA-only family): take its second queue if it has one
            if (computeFamily == UINT32_MAX && transferFamily != UINT32_MAX
                && (queueFamilyProperties[transferFamily].queueFlags & VK_QUEUE_COMPUTE_BIT)
                && queueFamilyProperties[transferFamily].queueCount >= 2) {
                computeFamily = transferFamily;
                computeQueueIndexForFamily = 1;
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

            bool hasDeviceFault = false;
            for (uint32_t j = 0; j < extCount; j++) {
                if (strcmp(exts[j].extensionName, VK_EXT_DEVICE_FAULT_EXTENSION_NAME) == 0) {
                    hasDeviceFault = true;
                    break;
                }
            }

#ifdef ENABLE_VULKAN_VALIDATION
            bool hasPipelineExecProps = false;
            for (uint32_t j = 0; j < extCount; j++) {
                if (strcmp(exts[j].extensionName, VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME) == 0) {
                    hasPipelineExecProps = true;
                    break;
                }
            }
#endif

            bool isDiscrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
            if (selected == VK_NULL_HANDLE || (isDiscrete && !selectedIsDiscrete)) {
                selected = pd;
                selectedGraphicsFamily = graphicsFamily;
                selectedTransferFamily = transferFamily;
                selectedComputeFamily = computeFamily;
                selectedComputeQueueIndex = computeQueueIndexForFamily;
                selectedMaintenance9 = hasMaintenance9;
                selectedMeshShaderQueries = static_cast<bool>(qMesh.meshShaderQueries);
                selectedDeviceFault = hasDeviceFault;
                selectedIsDiscrete = isDiscrete;
#ifdef ENABLE_VULKAN_VALIDATION
                selectedPipelineExecProps = hasPipelineExecProps;
#endif
            }
        }

        if (selected == VK_NULL_HANDLE) {
            LOG_ERROR(Engine, "No suitable GPU found; requires Vulkan 1.3, mesh shaders, and descriptor buffers");
            std::abort();
        }

        physicalDevice = selected;
        graphicsQueueFamily = selectedGraphicsFamily;
        transferQueueFamily = selectedTransferFamily;
        computeQueueFamily = selectedComputeFamily;
        computeQueueIndex = selectedComputeQueueIndex;
        bMaintenance9Enabled = selectedMaintenance9;
        bMeshShaderQueriesEnabled = selectedMeshShaderQueries;
        bDeviceFaultEnabled = selectedDeviceFault;
#ifdef ENABLE_VULKAN_VALIDATION
        bPipelineExecutablePropertiesEnabled = selectedPipelineExecProps;
#endif
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
        features12.scalarBlockLayout = VK_TRUE;
        features12.timelineSemaphore = VK_TRUE;

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
        features10.pipelineStatisticsQuery = VK_TRUE;

        // Shader types (core)
        features10.shaderInt16 = VK_TRUE;
        features10.shaderInt64 = VK_TRUE;

        // Gather / clip
        features10.shaderImageGatherExtended = VK_TRUE;
        features10.shaderClipDistance = VK_TRUE;
        features10.samplerAnisotropy = VK_TRUE;
        features10.fillModeNonSolid = VK_TRUE;
        features10.depthClamp = VK_TRUE;

        // BC Compression
        features10.textureCompressionBC = VK_TRUE;
#ifdef ENABLE_VULKAN_VALIDATION
        // Suppresses a false-positive validation error: SV_PrimitiveID in mesh shaders
        // triggers a geometry shader requirement check that doesn't apply here.
        features10.geometryShader = VK_TRUE;
#endif

        // Extensions
        descriptorBufferFeatures.descriptorBuffer = VK_TRUE;
        meshShaderFeatures.taskShader = VK_TRUE;
        meshShaderFeatures.meshShader = VK_TRUE;
        if (bMeshShaderQueriesEnabled) {
            meshShaderFeatures.meshShaderQueries = VK_TRUE;
        }
        extendedDynamicState3Features.extendedDynamicState3PolygonMode = VK_TRUE;
        accelerationStructureFeatures.accelerationStructure = VK_TRUE;
        rayQueryFeatures.rayQuery = VK_TRUE;
        if (bMaintenance9Enabled) {
            maintenance9Features.maintenance9 = VK_TRUE;
        }

        features13.pNext = &features12;
        features12.pNext = &features11;
        features11.pNext = &descriptorBufferFeatures;
        descriptorBufferFeatures.pNext = &meshShaderFeatures;
        meshShaderFeatures.pNext = &extendedDynamicState3Features;
        extendedDynamicState3Features.pNext = &accelerationStructureFeatures;
        accelerationStructureFeatures.pNext = &rayQueryFeatures;
        rayQueryFeatures.pNext = bMaintenance9Enabled ? static_cast<void*>(&maintenance9Features) : nullptr;
#ifdef ENABLE_VULKAN_VALIDATION
        if (bPipelineExecutablePropertiesEnabled) {
            pipelineExecutablePropertiesFeatures.pipelineExecutableInfo = VK_TRUE;
            if (bMaintenance9Enabled) {
                maintenance9Features.pNext = &pipelineExecutablePropertiesFeatures;
            }
            else {
                rayQueryFeatures.pNext = &pipelineExecutablePropertiesFeatures;
            }
        }
#endif
        if (bDeviceFaultEnabled) {
            deviceFaultFeatures.deviceFault = VK_TRUE;
            deviceFaultFeatures.pNext = features11.pNext;
            features11.pNext = &deviceFaultFeatures;
        }
#ifdef WDEBUG
        uint32_t addressBindingExtCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &addressBindingExtCount, nullptr);
        assert(addressBindingExtCount <= 512);
        Core::Array<VkExtensionProperties, 512> addressBindingExts;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &addressBindingExtCount, addressBindingExts.Data());
        for (uint32_t i = 0; i < addressBindingExtCount; ++i) {
            if (strcmp(addressBindingExts[i].extensionName, VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME) == 0) {
                bAddressBindingEnabled = addressBindingMessenger != VK_NULL_HANDLE;
            }
            if (strcmp(addressBindingExts[i].extensionName, VK_AMD_BUFFER_MARKER_EXTENSION_NAME) == 0) {
                bGpuMarkersEnabled = true;
            }
        }
        if (bAddressBindingEnabled) {
            addressBindingFeatures.reportAddressBinding = VK_TRUE;
            addressBindingFeatures.pNext = features11.pNext;
            features11.pNext = &addressBindingFeatures;
        }
#endif

        Core::InlineVector<const char*, 16> deviceExts;
        deviceExts.PushBack(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        deviceExts.PushBack(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
        deviceExts.PushBack(VK_EXT_MESH_SHADER_EXTENSION_NAME);
        deviceExts.PushBack(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        deviceExts.PushBack(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        deviceExts.PushBack(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        deviceExts.PushBack(VK_KHR_RAY_QUERY_EXTENSION_NAME);
#if PROFILER_ENABLED
        deviceExts.PushBack(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
#endif
        if (bMaintenance9Enabled) {
            deviceExts.PushBack(VK_KHR_MAINTENANCE_9_EXTENSION_NAME);
        }
        if (bDeviceFaultEnabled) {
            deviceExts.PushBack(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
        }
#ifdef WDEBUG
        if (bAddressBindingEnabled) {
            deviceExts.PushBack(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME);
        }
        if (bGpuMarkersEnabled) {
            deviceExts.PushBack(VK_AMD_BUFFER_MARKER_EXTENSION_NAME);
        }
#endif
#ifdef ENABLE_VULKAN_VALIDATION
        if (bPipelineExecutablePropertiesEnabled) {
            deviceExts.PushBack(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME);
        }
#endif

        float queuePriorities[2] = {1.0f, 1.0f};
        const uint32_t transferQueueCount = computeQueueFamily == transferQueueFamily ? 2u : 1u;
        Core::InlineVector<VkDeviceQueueCreateInfo, 3> queueInfos;
        queueInfos.PushBack(VkDeviceQueueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, graphicsQueueFamily, 1, queuePriorities});
        queueInfos.PushBack(VkDeviceQueueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, transferQueueFamily, transferQueueCount, queuePriorities});
        if (computeQueueFamily != UINT32_MAX && computeQueueFamily != transferQueueFamily) {
            queueInfos.PushBack(VkDeviceQueueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, computeQueueFamily, 1, queuePriorities});
        }

        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pNext = &features13;
        deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.Size());
        deviceCreateInfo.pQueueCreateInfos = queueInfos.Data();
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExts.Size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExts.Data();
        deviceCreateInfo.pEnabledFeatures = &features10;

        res = vkCreateDevice(physicalDevice, &deviceCreateInfo, &hostAllocationCallbacks, &device);
        if (res != VK_SUCCESS) {
            LOG_ERROR(Engine, "Failed to create Vulkan device: {}", string_VkResult(res));
            std::abort();
        }
        volkLoadDevice(device);
        if (bDeviceFaultEnabled) { gFaultReportDevice = device; }
    }

    vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, transferQueueFamily, 0, &transferQueue);
    if (computeQueueFamily != UINT32_MAX) {
        vkGetDeviceQueue(device, computeQueueFamily, computeQueueIndex, &computeQueue);
    }

    if (graphicsQueueFamily == transferQueueFamily) {
        LOG_ERROR(Engine, "Graphics and transfer queue families are the same ({})", graphicsQueueFamily);
        LOG_ERROR(Engine, "Your system may not support Vulkan");
        std::abort();
    }

    bufferSharingFamilies[bufferSharingFamilyCount++] = graphicsQueueFamily;
    bufferSharingFamilies[bufferSharingFamilyCount++] = transferQueueFamily;
    if (computeQueueFamily != UINT32_MAX && computeQueueFamily != graphicsQueueFamily && computeQueueFamily != transferQueueFamily) {
        bufferSharingFamilies[bufferSharingFamilyCount++] = computeQueueFamily;
    }
    imageSharingFamilies[imageSharingFamilyCount++] = graphicsQueueFamily;
    if (computeQueueFamily != UINT32_MAX && computeQueueFamily != graphicsQueueFamily) {
        imageSharingFamilies[imageSharingFamilyCount++] = computeQueueFamily;
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
    allocatorInfo.pAllocationCallbacks = &hostAllocationCallbacks;
#ifdef WDEBUG
    VmaDeviceMemoryCallbacks deviceMemoryCallbacks{};
    deviceMemoryCallbacks.pfnAllocate = VmaDeviceAllocate;
    deviceMemoryCallbacks.pfnFree = VmaDeviceFree;
    deviceMemoryCallbacks.pUserData = &memoryManager;
    allocatorInfo.pDeviceMemoryCallbacks = &deviceMemoryCallbacks;
#endif
    vmaCreateAllocator(&allocatorInfo, &allocator);

#ifdef WDEBUG
    if (bGpuMarkersEnabled) {
        VkBufferCreateInfo markerBufferInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        markerBufferInfo.size = 4 * sizeof(uint32_t);
        markerBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        const VkBufferCreateInfo sharedMarkerBufferInfo = ApplyBufferSharing(markerBufferInfo);
        VmaAllocationCreateInfo markerAllocInfo{};
        markerAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        markerAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo markerInfo{};
        res = vmaCreateBuffer(allocator, &sharedMarkerBufferInfo, &markerAllocInfo, &gGpuMarkerBuffer, &gGpuMarkerAllocation, &markerInfo);
        if (res == VK_SUCCESS) {
            gGpuMarkerValues = static_cast<volatile uint32_t*>(markerInfo.pMappedData);
        }
        else {
            LOG_ERROR(Engine, "Failed to create GPU marker buffer: {}", string_VkResult(res));
        }
    }
#endif

    deviceInfo.properties.pNext = &deviceInfo.descriptorBufferProps;
    deviceInfo.descriptorBufferProps.pNext = &deviceInfo.meshShaderProps;
    deviceInfo.meshShaderProps.pNext = &deviceInfo.subgroupProps;
    deviceInfo.subgroupProps.pNext = &deviceInfo.accelerationStructureProps;
    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceInfo.properties);

    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceInfo.memoryProperties);
    deviceInfo.bREBAR = !bForceNoREBAR && DetectREBAR(deviceInfo.memoryProperties);

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
    if (computeQueue != VK_NULL_HANDLE) {
        LOG_INFO(Engine, "Queue Families - Graphics: {} | Transfer: {} | Compute: {} (queue {})", graphicsQueueFamily, transferQueueFamily, computeQueueFamily, computeQueueIndex);
    }
    else {
        LOG_INFO(Engine, "Queue Families - Graphics: {} | Transfer: {} | Compute: none, aliased to graphics", graphicsQueueFamily, transferQueueFamily);
    }
    LOG_INFO(Engine, "Resizable BAR: {}", deviceInfo.bREBAR ? "yes" : (bForceNoREBAR ? "no (forced off)" : "no"));
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
#if PROFILER_ENABLED
    TracyVkDestroy(tracyContext);
#endif

#ifdef WDEBUG
    if (gGpuMarkerBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, gGpuMarkerBuffer, gGpuMarkerAllocation);
        gGpuMarkerBuffer = VK_NULL_HANDLE;
        gGpuMarkerValues = nullptr;
    }
#endif

    if (allocator) {
        vmaDestroyAllocator(allocator);
    }

    if (instance && surface) {
        vkDestroySurfaceKHR(instance, surface, &hostAllocationCallbacks);
    }

    if (device) {
        vkDestroyDevice(device, &hostAllocationCallbacks);
    }

    if (debugMessenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, &hostAllocationCallbacks);
    }

    if (addressBindingMessenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance, addressBindingMessenger, &hostAllocationCallbacks);
    }

    if (instance) {
        vkDestroyInstance(instance, &hostAllocationCallbacks);
    }
}
} // Renderer
