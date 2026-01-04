//
// Created by William on 2025-10-09.
//

#ifndef WILLENGINETESTBED_IMGUI_H
#define WILLENGINETESTBED_IMGUI_H

#include <volk.h>

#include "SDL3/SDL_events.h"

struct SDL_Window;

namespace Render
{
struct VulkanContext;

struct ImguiWrapper
{
public:
    ImguiWrapper() = default;

    ImguiWrapper(VulkanContext* context, SDL_Window* window, int32_t swapchainImageCount, VkFormat swapchainFormat);

    ~ImguiWrapper();

    static void HandleInput(const SDL_Event& e);

private:
    VulkanContext* context{nullptr};
    VkDescriptorPool imguiPool{VK_NULL_HANDLE};
};
}

#endif //WILLENGINETESTBED_IMGUI_H
