//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_CONTEXT_H
#define WILL_ENGINE_ENGINE_CONTEXT_H

#include <cstdint>
#include <atomic>

#include "core/containers/inline_function.h"

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
class MemoryManager;

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
    WindowContext windowContext{};

    enki::TaskScheduler* scheduler{nullptr};
    MemoryManager* memoryManager{nullptr};

    Engine::EngineLogger* engineLogger{nullptr};
    Engine::AssetManager* assetManager{nullptr};
    Engine::MaterialManager* materialManager{nullptr};
    Audio::AudioManager* audioManager{nullptr};
    Physics::PhysicsSystem* physicsSystem{nullptr};

    // Global Fn
    void (*internStringFn)(uint64_t, const char*);
    const char* (*resolveStringIdFn)(uint64_t);
    InlineFunction<void(bool)> setCursorHiddenFn;

    // Imgui
    ImGuiContext* imguiContext;
    void* (*imguiAllocFn)(size_t, void*){nullptr};
    void  (*imguiFreeFn)(void*, void*){nullptr};
    void* imguiAllocUserData{nullptr};
    bool bImguiKeyboardCaptured = false;
    bool bImguiMouseCaptured = false;
    bool bImGuiWantsTextInput = false;
    uint64_t lastKnownStableIdUnderCursor{0};

    uint64_t currentFrame{0};
    bool bModelLoadedThisFrame{false};


    // ImGui texture preview (routed through engine DLL where Vulkan fn ptrs are loaded)
    // handles are opaque uint64_t (VkSampler, VkImageView, VkDescriptorSet)
    InlineFunction<uint64_t(uint64_t, uint64_t)> addImguiTextureFn;
    InlineFunction<void(uint64_t)> removeImguiTextureFn;

    std::atomic<bool> bShouldRescanResources{false};
    std::atomic<bool> bShouldRescanMaterials{false};
};
} // Core

#endif //WILL_ENGINE_ENGINE_CONTEXT_H
