//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_CONTEXT_H
#define WILL_ENGINE_ENGINE_CONTEXT_H

#include <cstdint>
#include <atomic>
#include <utility>
#include <clay.h>

#include "core/containers/inline_function.h"
#include "core/containers/heap_array.h"
#include "core/containers/inline_path.h"
#include "core/memory/arena_suballocator.h"
#include "engine/resources/environment_map/probe_format.h"
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

/** Game-side landing zone for a captured probe face; pixels are S x S RGBA16F half-floats (4 per texel). */
struct ProbeCaptureStaging
{
    Core::HeapArray<uint16_t> pixels{};
    uint32_t captureSize{0};
    std::atomic<bool> bReady{false};
};

/** Game -> engine handoff of a completed probe bake for engine-side assembly into a filtered cubemap asset. Face buffers are S x S RGBA16F, moved in. */
struct ProbeAssembleStaging
{
    Core::HeapArray<uint16_t> faces[6]{};
    uint32_t captureSize{0};
    uint32_t targetResolution{0};
    Core::Path outputPath{};
    uint64_t probeId{0};
    ProbeBakeSnapshot snapshot{};
    std::atomic<bool> bPending{false};
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

    ProbeCaptureStaging probeCapture{};

    /** @returns true when a probe-face capture has been delivered by the renderer and not yet consumed. */
    bool IsProbeCaptureReady() const { return probeCapture.bReady.load(std::memory_order_acquire); }
    const ProbeCaptureStaging& GetProbeCapture() const { return probeCapture; }
    void ConsumeProbeCapture() { probeCapture.bReady.store(false, std::memory_order_release); }

    ProbeAssembleStaging probeAssemble{};

    /** Hands a finished bake's 6 face buffers off (moved) for engine-side assembly; the engine forwards it to the asset generator next frame. */
    void SubmitProbeAssemble(Core::HeapArray<uint16_t>* faces, uint32_t captureSize, uint32_t targetResolution, const Core::Path& outputPath, uint64_t probeId, const ProbeBakeSnapshot& snapshot)
    {
        for (uint32_t face = 0; face < 6; ++face) { probeAssemble.faces[face] = std::move(faces[face]); }
        probeAssemble.captureSize = captureSize;
        probeAssemble.targetResolution = targetResolution;
        probeAssemble.outputPath = outputPath;
        probeAssemble.probeId = probeId;
        probeAssemble.snapshot = snapshot;
        probeAssemble.bPending.store(true, std::memory_order_release);
    }

    /** @returns true when a bake has been submitted for assembly and not yet forwarded. */
    bool IsProbeAssemblePending() const { return probeAssemble.bPending.load(std::memory_order_acquire); }


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
