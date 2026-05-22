//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_CONTEXT_H
#define WILL_ENGINE_ENGINE_CONTEXT_H

#include <cstdint>
#include <atomic>
#include <clay.h>

#include "core/containers/inline_function.h"
#include "core/memory/arena_suballocator.h"
#include "render/pipelines/pipeline_manager.h"

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
} // Core

namespace Engine
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
    WindowContext windowContext{};

    enki::TaskScheduler* scheduler{nullptr};
    Core::MemoryManager* memoryManager{nullptr};
    Core::ManagedArena gameplayArena{};
    Core::ManagedArena editorArena{};

    EngineLogger* engineLogger{nullptr};
    AssetManager* assetManager{nullptr};
    MaterialManager* materialManager{nullptr};
    Render::PipelineManager* pipelineManager{nullptr};
    Audio::AudioManager* audioManager{nullptr};
    Physics::PhysicsSystem* physicsSystem{nullptr};

    // Global Fn
    void (*internStringFn)(uint64_t, const char*);
    const char* (*resolveStringIdFn)(uint64_t);
    Core::InlineFunction<void(bool)> setCursorHiddenFn;

    // Imgui
    ImGuiContext* imguiContext;
    void* (*imguiAllocFn)(size_t, void*){nullptr};
    void  (*imguiFreeFn)(void*, void*){nullptr};
    void* imguiAllocUserData{nullptr};
    bool bImguiKeyboardCaptured = false;
    bool bImguiMouseCaptured = false;
    bool bImGuiWantsTextInput = false;
    uint64_t lastKnownStableIdUnderCursor{0};

    // Clay
    Clay_Context* clayContext{nullptr};

    uint64_t currentRenderFrame{0};
    bool bModelLoadedThisFrame{false};


    // ImGui texture preview (routed through engine DLL where Vulkan fn ptrs are loaded)
    // handles are opaque uint64_t (VkSampler, VkImageView, VkDescriptorSet)
    Core::InlineFunction<uint64_t(uint64_t, uint64_t)> addImguiTextureFn;
    Core::InlineFunction<void(uint64_t)> removeImguiTextureFn;

    bool bShouldRescanResources{false};
    std::atomic<bool> bShouldRescanMaterials{false};
};
} // Engine

#endif //WILL_ENGINE_ENGINE_CONTEXT_H
