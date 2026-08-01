//
// Created by William on 2026-07-22.
//

#include "probe_bake_system.h"

#include <cstring>
#include <glm/gtc/constants.hpp>

#include "camera_system.h"
#include "ddgi_converge_boost.h"
#include "render_systems.h"
#include "game/game_state.h"
#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/core/probe_id.h"
#include "engine/logging/engine_log.h"
#include "platform/paths.h"
#include "platform/file_utils.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_path.h"
#include "engine/profiles/profile_library.h"
#include "game/components/core_components.h"
#include "game/components/render/reflection_probe_component.h"
#include "game/gameplay/camera/gameplay_camera.h"

namespace Game
{
constexpr float kProbeBakeOverscan = 1.2f;

struct ProbeFaceOrientation
{
    glm::vec3 forward;
    glm::vec3 up;
};

// Vulkan/KTX2 cube layer order: +X, -X, +Y, -Y, +Z, -Z (Y-up, GLM).
// Y rows are swapped in this engine though
static const ProbeFaceOrientation kProbeFaceOrientations[6] = {
    {{ 1.0f,  0.0f,  0.0f}, { 0.0f, -1.0f,  0.0f}},
    {{-1.0f,  0.0f,  0.0f}, { 0.0f, -1.0f,  0.0f}},
    {{ 0.0f, -1.0f,  0.0f}, { 0.0f,  0.0f, -1.0f}},
    {{ 0.0f,  1.0f,  0.0f}, { 0.0f,  0.0f,  1.0f}},
    {{ 0.0f,  0.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}},
    {{ 0.0f,  0.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}},
};

ProbeBakeSystem& ProbeBakeGet(Engine::EngineContext* ctx)
{
    return ctx->GetGameState<GameState>()->probeBake;
}

bool ProbeBakeActive(Engine::EngineContext* ctx)
{
    return ProbeBakeGet(ctx).bBakeActive;
}

void ProbeBakeTick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ProbeBakeGet(ctx).Tick(ctx, state, frameBuffer);
}

void ProbeBakeOverrideView(Engine::EngineContext* ctx, Core::ViewFamily& viewFamily)
{
    const ProbeBakeSystem& bake = ProbeBakeGet(ctx);
    if (bake.bViewOverrideActive) {
        viewFamily.mainView.currentViewData = bake.overrideView;
        viewFamily.mainView.previousViewData = bake.overrideView;
    }
}

void ProbeBakeScrubFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    if (!ProbeBakeActive(ctx)) { return; }

    Core::ViewFamily& viewFamily = frameBuffer->mainViewFamily;
    viewFamily.debugLines.Clear();
    viewFamily.debugBoxes.Clear();
    viewFamily.debugSpheres.Clear();
    viewFamily.debugRects.Clear();
    viewFamily.debugArrows.Clear();
    viewFamily.debugCapsules.Clear();
    viewFamily.debugCylinders.Clear();

    frameBuffer->selectedStableId = 0;
    frameBuffer->debug.bEnableGPUDebug = false;
}

void ProbeBakeSystem::Start(Engine::EngineContext* ctx, Engine::EngineState* state, entt::entity probe)
{
    if (bBakeActive) { return; }
    probeEntity = probe;
    currentFace = 0;
    settleCounter = 0;
    captureSize = 0;
    bakeCropSize = 0;
    bakeForcedExtent = 0;
    bFacesReady = false;
    bDryRun = false;
    bBakeActive = true;
    phase = Phase::Starting;
}

void ProbeBakeSystem::StartDryRun(Engine::EngineContext* ctx, Engine::EngineState* state, entt::entity probe)
{
    if (bBakeActive) { return; }
    Start(ctx, state, probe);
    bDryRun = bBakeActive;
}

void ProbeBakeSystem::EnqueueAllProbes(Engine::EngineState* state)
{
    bakeQueue.Clear();
    for (const entt::entity entity : state->registry.view<Component::ReflectionProbeComponent>()) {
        if (bakeQueue.IsFull()) { break; }
        bakeQueue.PushBack(entity);
    }
    bakeBatchTotal = static_cast<uint32_t>(bakeQueue.Size());
}

void ProbeBakeSystem::EnqueueAllProbesInterbounce(Engine::EngineState* state)
{
    if (phase == Phase::AwaitAssembles) {
        state->inputContext = stashedBatchInputContext;
        phase = Phase::Idle;
    }
    EnqueueAllProbes(state);
    interbounceBatch.Clear();
    for (const entt::entity entity : bakeQueue) {
        if (interbounceBatch.IsFull()) { break; }
        interbounceBatch.PushBack(entity);
    }
    awaitedAssembles.Clear();
    bInterbounceBatch = true;
    bakePass = 1;
}

void ProbeBakeSystem::EnqueueProbeInterbounce(Engine::EngineState* state, entt::entity probe)
{
    if (phase == Phase::AwaitAssembles) {
        state->inputContext = stashedBatchInputContext;
        phase = Phase::Idle;
    }
    bakeQueue.Clear();
    bakeQueue.PushBack(probe);
    bakeBatchTotal = 1u;
    interbounceBatch.Clear();
    interbounceBatch.PushBack(probe);
    awaitedAssembles.Clear();
    bInterbounceBatch = true;
    bakePass = 1;
}

void ProbeBakeSystem::Cancel(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (bBakeActive) {
        state->lighting.reflectionProbe = stashedProbeConfig;
        state->debug.bGIFreeze = stashedGIFreeze;
        ClearProbeBakeHideSet(ctx, state);
        state->inputContext = stashedInputContext;
        if (bakeForcedExtent > 0) {
            state->projectConfig.resolutionScale = stashedResolutionScale;
            bPendingExtentRestore = true;
            bakeForcedExtent = 0;
        }
    } else if (phase == Phase::AwaitAssembles) {
        state->inputContext = stashedBatchInputContext;
    }
    bakeQueue.Clear();
    bakeBatchTotal = 0;
    bInterbounceBatch = false;
    interbounceBatch.Clear();
    awaitedAssembles.Clear();
    bakePass = 1;
    bViewOverrideActive = false;
    bBakeActive = false;
    bDryRun = false;
    phase = Phase::Idle;
}

void ProbeBakeSystem::Tick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->bCaptureProbeFace = false;
    frameBuffer->probeCaptureCropSize = bBakeActive ? bakeCropSize : 0;

    if (bPendingExtentRestore) {
        frameBuffer->viewportResizeCommand = {true, stashedViewportOffsetX, stashedViewportOffsetY, stashedViewportWidth, stashedViewportHeight};
        bPendingExtentRestore = false;
    }

    // A mid-bake panel drag is not blocked by the input lockout; re-stash the user's new viewport and keep the forced extent so the 90-degree crop stays exact
    if (bBakeActive && bakeForcedExtent > 0 && frameBuffer->viewportResizeCommand.bEngineCommandsResize) {
        stashedViewportOffsetX = frameBuffer->viewportResizeCommand.offsetX;
        stashedViewportOffsetY = frameBuffer->viewportResizeCommand.offsetY;
        stashedViewportWidth = frameBuffer->viewportResizeCommand.sizeX;
        stashedViewportHeight = frameBuffer->viewportResizeCommand.sizeY;
        frameBuffer->viewportResizeCommand = {true, stashedViewportOffsetX, stashedViewportOffsetY, bakeForcedExtent, bakeForcedExtent};
    }

    switch (phase) {
        case Phase::Idle:
        {
            for (auto [entity, probe] : state->registry.view<Component::ReflectionProbeComponent>().each()) {
                if (!probe.bBakeRequested) { continue; }
                probe.bBakeRequested = false;
                if (!bakeQueue.Contains(entity) && !bakeQueue.IsFull()) { bakeQueue.PushBack(entity); }
            }

            while (!bakeQueue.IsEmpty()) {
                const entt::entity next = bakeQueue.Front();
                bakeQueue.RemoveAt(0);
                if (state->registry.valid(next) && state->registry.all_of<Component::ReflectionProbeComponent>(next)) {
                    Start(ctx, state, next);
                    return;
                }
            }

            if (bInterbounceBatch) {
                if (bakePass == 1) {
                    stashedBatchInputContext = state->inputContext == Engine::InputContext::Console ? Engine::InputContext::Editor : state->inputContext;
                    state->inputContext = Engine::InputContext::ProbeBake;
                    awaitTimeoutFrames = 0;
                    phase = Phase::AwaitAssembles;
                    return;
                }
                bInterbounceBatch = false;
                interbounceBatch.Clear();
                awaitedAssembles.Clear();
                bakePass = 1;
            }

            bakeBatchTotal = 0;
            return;
        }
        case Phase::Starting:
        {
            stashedProbeConfig = state->lighting.reflectionProbe;

            if (bInterbounceBatch && bakePass == 2) {
                state->lighting.reflectionProbe.bEnabled = true;
                state->lighting.reflectionProbe.intensity = 1.0f;
            } else {
                state->lighting.reflectionProbe.bEnabled = false;
            }

            ApplyProbeBakeHideSet(ctx, state);
            state->editor.selectedEntities.Clear();

            // Console restores via its own prevContext once we steal the context; restoring Console here would strand input in a closed console
            stashedInputContext = state->inputContext == Engine::InputContext::Console ? Engine::InputContext::Editor : state->inputContext;
            state->inputContext = Engine::InputContext::ProbeBake;

            settleFrames = glm::max(1, state->projectConfig.probeBake.settleFrames);

            const Component::WorldTransformComponent* worldTransform = state->registry.try_get<Component::WorldTransformComponent>(probeEntity);
            const Component::ReflectionProbeComponent* probe = state->registry.try_get<Component::ReflectionProbeComponent>(probeEntity);
            if (!worldTransform || !probe) {
                Cancel(ctx, state);
                return;
            }
            capturePosition = worldTransform->translation + worldTransform->rotation * probe->captureOffset;
            bakeProbeId = probe->probeId;
            bakeTargetResolution = probe->resolution == Component::ReflectionProbeComponent::Resolution::Res128 ? 128 : 256;

            bakeSnapshot = {};
            bakeSnapshot.translation[0] = worldTransform->translation.x;
            bakeSnapshot.translation[1] = worldTransform->translation.y;
            bakeSnapshot.translation[2] = worldTransform->translation.z;
            bakeSnapshot.rotation[0] = worldTransform->rotation.w;
            bakeSnapshot.rotation[1] = worldTransform->rotation.x;
            bakeSnapshot.rotation[2] = worldTransform->rotation.y;
            bakeSnapshot.rotation[3] = worldTransform->rotation.z;
            bakeSnapshot.scale[0] = worldTransform->scale.x;
            bakeSnapshot.scale[1] = worldTransform->scale.y;
            bakeSnapshot.scale[2] = worldTransform->scale.z;
            bakeSnapshot.captureOffset[0] = probe->captureOffset.x;
            bakeSnapshot.captureOffset[1] = probe->captureOffset.y;
            bakeSnapshot.captureOffset[2] = probe->captureOffset.z;

            const int32_t crop = glm::clamp(state->projectConfig.probeBake.captureSize, 256, 1280);
            bakeCropSize = static_cast<uint32_t>(crop) & ~1u;
            const uint32_t overscanned = static_cast<uint32_t>(glm::ceil(static_cast<float>(bakeCropSize) * kProbeBakeOverscan));
            bakeForcedExtent = (overscanned + 1u) & ~1u;

            stashedViewportOffsetX = ctx->windowContext.viewportOffsetX;
            stashedViewportOffsetY = ctx->windowContext.viewportOffsetY;
            stashedViewportWidth = ctx->windowContext.viewportWidth;
            stashedViewportHeight = ctx->windowContext.viewportHeight;
            stashedResolutionScale = state->projectConfig.resolutionScale;

            // Force a square native-res render target so the exact-90-degree crop and fovY are aspect-independent.
            state->projectConfig.resolutionScale = 1.0f;
            frameBuffer->viewportResizeCommand = {true, stashedViewportOffsetX, stashedViewportOffsetY, bakeForcedExtent, bakeForcedExtent};

            stashedGIFreeze = state->debug.bGIFreeze;
            state->debug.bGIFreeze = false;
            if (state->projectConfig.probeBake.bAutoConverge) {
                DDGIConvergeBoostTrigger(state->ddgiConvergeBoost, state->lighting.ddgi);
            }

            phase = Phase::FaceSetup;
            return;
        }
        case Phase::FaceSetup:
        {
            const float aspect = 1.0f;
            const float fovY = 2.0f * glm::atan(static_cast<float>(bakeForcedExtent) / static_cast<float>(bakeCropSize));

            const ProbeFaceOrientation& face = kProbeFaceOrientations[currentFace];
            overrideView = Camera::BuildPerspectiveView(capturePosition, face.forward, face.up, aspect, fovY, state->projectConfig.editorCameraNearPlane);
            bViewOverrideActive = true;

            state->requests.pendingCacheReset = Core::RenderCacheReset::ScreenHistory;
            settleCounter = 0;
            phase = Phase::Settling;
            return;
        }
        case Phase::Settling:
        {
            ++settleCounter;
            if (settleCounter >= settleFrames) {
                if (currentFace == 0 && state->ddgiConvergeBoost.bActive) {
                    return;
                }
                if (state->projectConfig.probeBake.bAutoFreeze) {
                    state->debug.bGIFreeze = true;
                }
                if (bDryRun) {
                    ++currentFace;
                    phase = (currentFace >= 6) ? Phase::Finishing : Phase::FaceSetup;
                    return;
                }
                phase = Phase::CaptureRequested;
            }
            return;
        }
        case Phase::CaptureRequested:
        {
            // Drain any stale pre-bake delivery so it cannot be mistaken for this face
            if (ctx->IsProbeCaptureReady()) {
                ctx->ConsumeProbeCapture();
            }
            frameBuffer->bCaptureProbeFace = true;
            phase = Phase::AwaitDelivery;
            return;
        }
        case Phase::AwaitDelivery:
        {
            if (!ctx->IsProbeCaptureReady()) { return; }

            const Engine::ProbeCaptureStaging& capture = ctx->GetProbeCapture();
            captureSize = capture.captureSize;
            if (captureSize > 0 && capture.pixels.IsAllocated()) {
                const size_t halfCount = static_cast<size_t>(captureSize) * captureSize * 4;
                faceBuffers[currentFace] = Core::HeapArray<uint16_t>(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::EngineContext, halfCount);
                std::memcpy(faceBuffers[currentFace].Data(), capture.pixels.Data(), halfCount * sizeof(uint16_t));
            }
            ctx->ConsumeProbeCapture();

            ++currentFace;
            if (currentFace >= 6) {
                phase = Phase::Finishing;
            } else {
                phase = Phase::FaceSetup;
            }
            return;
        }
        case Phase::Finishing:
        {
            state->lighting.reflectionProbe = stashedProbeConfig;
            state->debug.bGIFreeze = stashedGIFreeze;
            ClearProbeBakeHideSet(ctx, state);
            state->inputContext = stashedInputContext;
            if (bakeForcedExtent > 0) {
                state->projectConfig.resolutionScale = stashedResolutionScale;
                bPendingExtentRestore = true;
                bakeForcedExtent = 0;
            }
            bViewOverrideActive = false;
            bBakeActive = false;
            bFacesReady = !bDryRun;
            bDryRun = false;

            bool bAllFacesReady = captureSize > 0;
            for (const auto & faceBuffer : faceBuffers) {
                if (!faceBuffer.IsAllocated()) { bAllFacesReady = false; }
            }
            if (bAllFacesReady) {
                Core::InlineString<64> assetName = Core::InlineString<64>::Format("probe_%llu.wprobe", static_cast<unsigned long long>(bakeProbeId));
                Core::Path outputPath = Platform::GetAssetPath() / "probes" / assetName.c_str();
                ctx->SubmitProbeAssemble(faceBuffers, captureSize, bakeTargetResolution, outputPath, bakeProbeId, bakeSnapshot);

                if (bInterbounceBatch && bakePass == 1 && !awaitedAssembles.IsFull()) {
                    const Engine::AssetManager::ProbeInfo* info = ctx->assetManager->GetProbeInfo(Engine::ProbeID{bakeProbeId});
                    const uint64_t expectedVersion = (info ? info->contentVersion : 0) + 1;
                    awaitedAssembles.PushBack(AwaitedAssemble{bakeProbeId, expectedVersion, probeEntity});
                }
            }

            phase = Phase::Idle;
            return;
        }
        case Phase::AwaitAssembles:
        {
            ++awaitTimeoutFrames;

            Core::InlineVector<AwaitedAssemble, 64> stillPending{};
            bool bAllReady = true;
            for (const AwaitedAssemble& awaited : awaitedAssembles) {
                if (!state->registry.valid(awaited.entity) || !state->registry.all_of<Component::ReflectionProbeComponent>(awaited.entity)) { continue; }
                stillPending.PushBack(awaited);

                const Component::ReflectionProbeComponent& probe = state->registry.get<Component::ReflectionProbeComponent>(awaited.entity);
                const Engine::AssetManager::ProbeInfo* info = ctx->assetManager->GetProbeInfo(Engine::ProbeID{awaited.probeId});
                bool bReady = info != nullptr
                    && info->contentVersion >= awaited.expectedVersion
                    && probe.contentSource == Component::ReflectionProbeComponent::ContentSource::Baked
                    && probe.contentHandle.IsValid();
                if (bReady) {
                    const Render::Cubemap* cubemap = ctx->assetManager->GetCubemap(probe.contentHandle);
                    bReady = cubemap != nullptr && cubemap->loadState == Render::Cubemap::LoadState::Loaded;
                }
                if (!bReady) { bAllReady = false; }
            }
            awaitedAssembles = stillPending;

            if (awaitTimeoutFrames > 3600) {
                LOG_ERROR(Game, "Interbounce probe bake timed out waiting for pass-1 assembles; aborting pass 2.");
                state->inputContext = stashedBatchInputContext;
                bakeQueue.Clear();
                bakeBatchTotal = 0;
                bInterbounceBatch = false;
                interbounceBatch.Clear();
                awaitedAssembles.Clear();
                bakePass = 1;
                phase = Phase::Idle;
                return;
            }

            if (bAllReady) {
                state->inputContext = stashedBatchInputContext;
                bakePass = 2;
                bakeQueue.Clear();
                for (const entt::entity entity : interbounceBatch) {
                    if (bakeQueue.IsFull()) { break; }
                    if (state->registry.valid(entity) && state->registry.all_of<Component::ReflectionProbeComponent>(entity)) { bakeQueue.PushBack(entity); }
                }
                bakeBatchTotal = static_cast<uint32_t>(bakeQueue.Size());
                awaitedAssembles.Clear();
                phase = Phase::Idle;
            }
            return;
        }
    }
}
} // Game
