//
// Created by William on 2025-12-11.
//

#ifndef WILL_ENGINE_VK_UTILS_H
#define WILL_ENGINE_VK_UTILS_H

#include <spdlog/spdlog.h>
#include <vulkan/vk_enum_string_helper.h>

namespace Render
{
void ReportDeviceFault();

#ifdef WDEBUG
void BeginGpuMarkerFrame();

void WriteGpuPassMarker(VkCommandBuffer cmd, bool bAsync, bool bEnd, uint32_t passIndex, const char* passName);
#endif

#define VK_CHECK(x)                                                          \
    do {                                                                     \
        VkResult err = x;                                                    \
        if (err) {                                                           \
            SPDLOG_CRITICAL("Vulkan error: {}", string_VkResult(err));       \
            SPDLOG_CRITICAL("  at {}:{}", __FILE__, __LINE__);               \
            SPDLOG_CRITICAL("  call: {}", #x);                               \
            if (err == VK_ERROR_DEVICE_LOST) { Render::ReportDeviceFault(); } \
            std::abort();                                                    \
        }                                                                    \
    } while (0)
}

#endif //WILL_ENGINE_VK_UTILS_H
