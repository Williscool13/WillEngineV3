//
// Created by William on 2025-12-11.
//

#ifndef WILL_ENGINE_VK_UTILS_H
#define WILL_ENGINE_VK_UTILS_H

#include <spdlog/spdlog.h>
#include <vulkan/vk_enum_string_helper.h>

namespace Render
{
#define VK_CHECK(x)                                                          \
    do {                                                                     \
        VkResult err = x;                                                    \
        if (err) {                                                           \
            SPDLOG_CRITICAL("Vulkan error: {}", string_VkResult(err));       \
            SPDLOG_CRITICAL("  at {}:{}", __FILE__, __LINE__);               \
            SPDLOG_CRITICAL("  call: {}", #x);                               \
            std::abort();                                                    \
        }                                                                    \
    } while (0)
}

#endif //WILL_ENGINE_VK_UTILS_H
