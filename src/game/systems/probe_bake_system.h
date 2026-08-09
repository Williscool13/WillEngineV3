//
// Created by William on 2026-07-22.
//

#ifndef WILL_ENGINE_PROBE_BAKE_SYSTEM_H
#define WILL_ENGINE_PROBE_BAKE_SYSTEM_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/containers/heap_array.h"
#include "core/containers/inline_vector.h"
#include "engine/resources/environment_map/probe_format.h"
#include "render/interface/render_interface.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
enum class InputContext : uint8_t;
}

namespace Game
{
/**
 * Editor-side relection probe bake driver.
 * The 6 captured face buffers stay resident after a bake for the assembly checkpoint to consume.
 */
struct ProbeBakeSystem
{
    enum class Phase : uint8_t
    {
        Idle,
        Starting,
        FaceSetup,
        Settling,
        CaptureRequested,
        AwaitDelivery,
        Finishing,
        AwaitAssembles,
    };

    struct AwaitedAssemble
    {
        uint64_t probeId{0};
        uint64_t expectedVersion{0};
        entt::entity entity{entt::null};
    };

    Phase phase{Phase::Idle};
    /** True from the first setup frame until the restore completes; gates editor debug draws and the view override. */
    bool bBakeActive{false};
    bool bViewOverrideActive{false};
    /** Latched true once all 6 faces are delivered; buffers stay resident for the assembly checkpoint. */
    bool bFacesReady{false};
    /** Preview-only run: same face walk, hide set, and settle pacing, but no capture, assemble, or file output. */
    bool bDryRun{false};

    entt::entity probeEntity{entt::null};
    glm::vec3 capturePosition{0.0f};
    /** Stashed from the probe component at Starting; the entity may be deleted mid-bake. */
    uint64_t bakeProbeId{0};
    /** Baked-state snapshot stashed at Starting; written into the .wprobe header for stale-bake detection. */
    Engine::ProbeBakeSnapshot bakeSnapshot{};
    uint32_t bakeTargetResolution{256};
    int32_t currentFace{0};
    int32_t settleCounter{0};
    int32_t settleFrames{48};
    uint32_t captureSize{0};

    uint32_t bakeCropSize{0};
    uint32_t bakeForcedExtent{0};
    /** Viewport extent request stashed at Starting and restored at Finishing/Cancel. */
    uint32_t stashedViewportOffsetX{0};
    uint32_t stashedViewportOffsetY{0};
    uint32_t stashedViewportWidth{0};
    uint32_t stashedViewportHeight{0};
    float stashedResolutionScale{1.0f};
    /** Set by Finishing/Cancel; the next Tick re-issues the stashed viewport resize (Cancel has no FrameBuffer of its own). */
    bool bPendingExtentRestore{false};

    Core::ViewData overrideView{};
    Core::ReflectionProbeConfiguration stashedProbeConfig{};
    Engine::InputContext stashedInputContext{};
    bool stashedGIFreeze{false};
    /** Latched from ProbeBakeSettings::bGroundTruth at Starting so a mid-bake config toggle cannot half-apply. */
    bool bGroundTruthBake{false};
    Core::GroundTruthMode stashedGroundTruthMode{Core::GroundTruthMode::None};
    int32_t stashedGroundTruthSpp{1};
    Core::HeapArray<uint16_t> faceBuffers[6]{};

    /** Probe entities awaiting a bake; drained one at a time from the Idle phase. */
    Core::InlineVector<entt::entity, 64> bakeQueue{};
    /** Size of the last "bake all" batch, for the "probe K of N" progress readout. */
    uint32_t bakeBatchTotal{0};

    /** True while a two-pass interbounce "bake all" is running; pass 1 bakes cold, pass 2 rebakes with pass-1 results lit. */
    bool bInterbounceBatch{false};
    int32_t bakePass{1};
    /** Round-1 entity list, preserved to refill the queue for round 2. */
    Core::InlineVector<entt::entity, 64> interbounceBatch{};
    /** Pass-1 assembles the batch waits on before starting pass 2. */
    Core::InlineVector<AwaitedAssemble, 64> awaitedAssembles{};
    int32_t awaitTimeoutFrames{0};
    Engine::InputContext stashedBatchInputContext{};

    /** Kicks a bake for the given probe entity; no-op if a bake is already active. */
    void Start(Engine::EngineContext* ctx, Engine::EngineState* state, entt::entity probe);

    /** Kicks a preview-only walk of the given probe's faces; renders and settles exactly like a bake but captures and writes nothing. */
    void StartDryRun(Engine::EngineContext* ctx, Engine::EngineState* state, entt::entity probe);

    /** Queues every reflection-probe entity in the scene for baking; replaces any pending queue. */
    void EnqueueAllProbes(Engine::EngineState* state);

    /** Queues every reflection-probe entity for a two-pass interbounce bake; pass 2 rebakes with pass-1 results lit. */
    void EnqueueAllProbesInterbounce(Engine::EngineState* state);

    /** Queues a single probe entity for a two-pass interbounce bake; replaces any pending queue. */
    void EnqueueProbeInterbounce(Engine::EngineState* state, entt::entity probe);

    /** Advances the state machine one render frame; owns FrameBuffer::bCaptureProbeFace (cleared every tick, raised on request frames). */
    void Tick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);

    /** Runs the Finishing restoration from any state and returns to Idle without marking faces ready. */
    void Cancel(Engine::EngineContext* ctx, Engine::EngineState* state);
};

ProbeBakeSystem& ProbeBakeGet(Engine::EngineContext* ctx);

/** True while a bake is walking its faces. */
bool ProbeBakeActive(Engine::EngineContext* ctx);

/** Advances the bake state machine one render frame; call before BuildViewFamily so the override lands this frame. */
void ProbeBakeTick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);

/** Applies the active bake face's view override onto the main view; call as the last statement of BuildViewFamily so the override lands this frame. No-op when no override is active. */
void ProbeBakeOverrideView(Engine::EngineContext* ctx, Core::ViewFamily& viewFamily);

/** During an active bake, empties every main-view debug-draw array and suppresses the selection outline and GPU debug so nothing bakes into the capture. Call late in GamePrepareFrame after all debug producers. No-op otherwise. */
void ProbeBakeScrubFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
} // Game

#endif //WILL_ENGINE_PROBE_BAKE_SYSTEM_H
