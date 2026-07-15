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


struct WorldCacheStatsSnapshot
{
    uint32_t occupiedSlots{};
    uint32_t cellsCarried{};
    uint32_t cellsEvicted{};
    uint32_t insertsFailed{};
    uint32_t cellsShaded{};
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
    Core::InlineFunction<void(bool)> setTextInputActiveFn;

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
    /** Set when any model/font finished loading or a model/font was reclaimed this frame; gates the per-frame asset-resolve block. */
    bool bAssetsChangedThisFrame{false};

    WorldCacheStatsSnapshot worldCacheStats{};


    // ImGui texture preview (routed through engine DLL where Vulkan fn ptrs are loaded)
    // handles are opaque uint64_t (VkSampler, VkImageView, VkDescriptorSet)
    Core::InlineFunction<uint64_t(uint64_t, uint64_t)> addImguiTextureFn;
    Core::InlineFunction<void(uint64_t)> removeImguiTextureFn;

    bool bShouldRescanResources{false};
    std::atomic<bool> bShouldRescanMaterials{false};

    /**
     * Opaque, engine-allocated (persistent, sized via GameGetStateSize) storage for game-defined persistent state -
     * the engine never interprets it, only owns the memory so it survives game DLL hot-reload. Game code placement-
     * constructs its own type into it once (GameStartup) and destructs it once (GameShutdown); see GetGameState().
     */
    void* gameState{nullptr};
    size_t gameStateSize{0};

    template<typename T>
    T* GetGameState() { return static_cast<T*>(gameState); }
};
} // Engine

#endif //WILL_ENGINE_ENGINE_CONTEXT_H
