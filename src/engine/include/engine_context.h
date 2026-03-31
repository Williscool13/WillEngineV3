//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_CONTEXT_H
#define WILL_ENGINE_ENGINE_CONTEXT_H

#include <cstdint>
#include <functional>
#include <memory>

#include "spdlog/logger.h"

namespace enki
{
class TaskScheduler;
}

namespace Audio
{
class AudioManager;
}

namespace Physics
{
class PhysicsSystem;
}

namespace Render
{
struct VulkanContext;
struct ResourceManager;
}

namespace Engine
{
class MaterialManager;
class EngineLogger;
class AssetManager;
}

struct ImGuiContext;

namespace Core
{
struct WindowContext
{
    uint32_t windowWidth;
    uint32_t windowHeight;

    uint32_t viewportWidth;
    uint32_t viewportHeight;
    uint32_t viewportOffsetX;
    uint32_t viewportOffsetY;
};

struct EngineContext
{
    WindowContext windowContext;

    Engine::EngineLogger* engineLogger;

    // Imgui
    ImGuiContext* imguiContext;
    bool bImguiKeyboardCaptured = false;
    bool bImguiMouseCaptured = false;
    bool bImGuiWantsTextInput = false;
    uint64_t lastKnownStableIdUnderCursor{0};

    enki::TaskScheduler* scheduler;

    //Render::ResourceManager* resourceManager;
    Engine::AssetManager* assetManager{nullptr};
    Engine::MaterialManager* materialManager{nullptr};

    uint64_t currentFrame{0};
    bool bModelLoadedThisFrame{false};


    Audio::AudioManager* audioManager;
    Physics::PhysicsSystem* physicsSystem;

    // Global Fn
    void (*internStringFn)(uint64_t, const char*);
    const char* (*resolveStringIdFn)(uint64_t);
    std::function<void(bool)> setCursorHiddenFn;

    // ImGui texture preview (routed through engine DLL where Vulkan fn ptrs are loaded)
    // handles are opaque uint64_t (VkSampler, VkImageView, VkDescriptorSet)
    std::function<uint64_t(uint64_t sampler, uint64_t imageView)> addImguiTextureFn;
    std::function<void(uint64_t descriptorSet)> removeImguiTextureFn;

    std::atomic<bool> bShouldRescanResources{false};
    std::atomic<bool> bShouldRescanMaterials{false};
};
} // Core

#endif //WILL_ENGINE_ENGINE_CONTEXT_H
