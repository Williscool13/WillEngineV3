//
// Created by William on 2026-07-22.
//

#include "probe_bake_system.h"

#include <cstring>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>
#include <stb/stb_image_write.h>

#include "camera_system.h"
#include "render_systems.h"
#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "platform/paths.h"
#include "platform/file_utils.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_path.h"
#include "game/components/core_components.h"
#include "game/components/render/reflection_probe_component.h"
#include "game/gameplay/camera/gameplay_camera.h"

namespace Game
{
struct ProbeFaceOrientation
{
    glm::vec3 forward;
    glm::vec3 up;
};

// Vulkan/KTX2 cube layer order: +X, -X, +Y, -Y, +Z, -Z (Y-up, GLM). Handedness validated in checkpoint 3d against an axis-colored room.
static const ProbeFaceOrientation kProbeFaceOrientations[6] = {
    {{ 1.0f,  0.0f,  0.0f}, { 0.0f, -1.0f,  0.0f}},
    {{-1.0f,  0.0f,  0.0f}, { 0.0f, -1.0f,  0.0f}},
    {{ 0.0f,  1.0f,  0.0f}, { 0.0f,  0.0f,  1.0f}},
    {{ 0.0f, -1.0f,  0.0f}, { 0.0f,  0.0f, -1.0f}},
    {{ 0.0f,  0.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}},
    {{ 0.0f,  0.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}},
};

static void WriteProbeFaceHdr(Engine::EngineContext* ctx, const uint16_t* pixels, uint32_t square, const Core::Path& path)
{
    const uint32_t texelCount = square * square;
    Core::HeapArray<float> rgb(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::EngineContext, static_cast<size_t>(texelCount) * 3);
    for (uint32_t i = 0; i < texelCount; ++i) {
        const size_t src = static_cast<size_t>(i) * 4;
        const glm::vec2 rgChannels = glm::unpackHalf2x16(static_cast<uint32_t>(pixels[src + 0]) | (static_cast<uint32_t>(pixels[src + 1]) << 16u));
        const glm::vec2 baChannels = glm::unpackHalf2x16(static_cast<uint32_t>(pixels[src + 2]) | (static_cast<uint32_t>(pixels[src + 3]) << 16u));
        const size_t dst = static_cast<size_t>(i) * 3;
        rgb[dst + 0] = rgChannels.x;
        rgb[dst + 1] = rgChannels.y;
        rgb[dst + 2] = baChannels.x;
    }
    stbi_write_hdr(path.c_str(), static_cast<int>(square), static_cast<int>(square), 3, rgb.Data());
}

ProbeBakeSystem& ProbeBakeGetOrCreate(Engine::EngineState* state)
{
    if (auto* existing = state->registry.ctx().find<ProbeBakeSystem>()) {
        return *existing;
    }
    return state->registry.ctx().emplace<ProbeBakeSystem>();
}

ProbeBakeSystem* ProbeBakeFind(Engine::EngineState* state)
{
    return state->registry.ctx().find<ProbeBakeSystem>();
}

bool ProbeBakeActive(Engine::EngineState* state)
{
    const ProbeBakeSystem* bake = state->registry.ctx().find<ProbeBakeSystem>();
    return bake && bake->bBakeActive;
}

void ProbeBakeTick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    if (auto* bake = state->registry.ctx().find<ProbeBakeSystem>()) {
        bake->Tick(ctx, state, frameBuffer);
    }
}

void ProbeBakeOverrideView(Engine::EngineState* state, Core::ViewFamily& viewFamily)
{
    if (const ProbeBakeSystem* bake = ProbeBakeFind(state); bake && bake->bViewOverrideActive) {
        viewFamily.mainView.currentViewData = bake->overrideView;
        viewFamily.mainView.previousViewData = bake->overrideView;
    }
}

void ProbeBakeScrubFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    if (!ProbeBakeActive(state)) { return; }

    Core::ViewFamily& viewFamily = frameBuffer->mainViewFamily;
    viewFamily.debugLines.Clear();
    viewFamily.debugBoxes.Clear();
    viewFamily.debugSpheres.Clear();
    viewFamily.debugRects.Clear();
    viewFamily.debugArrows.Clear();
    viewFamily.debugCapsules.Clear();
    viewFamily.debugCylinders.Clear();

    frameBuffer->selectedStableId = 0;
    frameBuffer->bEnableGPUDebug = false;
}

void ProbeBakeSystem::Start(Engine::EngineContext* ctx, Engine::EngineState* state, entt::entity probe)
{
    if (bBakeActive) { return; }
    bManualDumpRequested = false;
    bManualDumpAwaiting = false;
    probeEntity = probe;
    currentFace = 0;
    settleCounter = 0;
    captureSize = 0;
    bFacesReady = false;
    bBakeActive = true;
    phase = Phase::Starting;
}

void ProbeBakeSystem::Cancel(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (bBakeActive) {
        state->lighting.reflectionProbe = stashedProbeConfig;
        state->lighting.reflection = stashedReflectionConfig;
        ClearProbeBakeHideSet(ctx, state);
        state->inputContext = stashedInputContext;
    }
    bViewOverrideActive = false;
    bBakeActive = false;
    phase = Phase::Idle;
}

void ProbeBakeSystem::Tick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->bCaptureProbeFace = false;

    if (!bBakeActive && bManualDumpRequested) {
        if (!bManualDumpAwaiting) {
            frameBuffer->bCaptureProbeFace = true;
            bManualDumpAwaiting = true;
        } else if (ctx->IsProbeCaptureReady()) {
            const Engine::ProbeCaptureStaging& capture = ctx->GetProbeCapture();
            if (capture.captureSize > 0 && capture.pixels.IsAllocated()) {
                static uint32_t probeCaptureCounter = 0;
                Core::Path captureDir = Platform::GetUserDataPath() / "screenshots";
                Platform::CreateDirectories(captureDir.c_str());
                Core::InlineString<64> captureName = Core::InlineString<64>::Format("probe_capture_%u.hdr", probeCaptureCounter++);
                Core::Path capturePath = captureDir / captureName.c_str();
                WriteProbeFaceHdr(ctx, capture.pixels.Data(), capture.captureSize, capturePath);
            }
            ctx->ConsumeProbeCapture();
            bManualDumpRequested = false;
            bManualDumpAwaiting = false;
        }
    }

    switch (phase) {
        case Phase::Idle:
        {
            return;
        }
        case Phase::Starting:
        {
            stashedProbeConfig = state->lighting.reflectionProbe;
            stashedReflectionConfig = state->lighting.reflection;

            state->lighting.reflection.bEnabled = true;
            state->lighting.reflectionProbe.bEnabled = false;

            ApplyProbeBakeHideSet(ctx, state);
            state->editor.selectedEntities.Clear();

            // Console restores via its own prevContext once we steal the context; restoring Console here would strand input in a closed console
            stashedInputContext = state->inputContext == Engine::InputContext::Console ? Engine::InputContext::Editor : state->inputContext;
            state->inputContext = Engine::InputContext::ProbeBake;

            settleFrames = glm::max(1, stashedProbeConfig.settleFrames);

            const Component::WorldTransformComponent* worldTransform = state->registry.try_get<Component::WorldTransformComponent>(probeEntity);
            const Component::ReflectionProbeComponent* probe = state->registry.try_get<Component::ReflectionProbeComponent>(probeEntity);
            if (!worldTransform || !probe) {
                Cancel(ctx, state);
                return;
            }
            capturePosition = worldTransform->translation + worldTransform->rotation * probe->captureOffset;

            phase = Phase::FaceSetup;
            return;
        }
        case Phase::FaceSetup:
        {
            const uint32_t viewportWidth = ctx->windowContext.viewportWidth;
            const uint32_t viewportHeight = ctx->windowContext.viewportHeight;
            const float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
            float fovY = glm::radians(90.0f);
            if (viewportWidth < viewportHeight) {
                fovY = 2.0f * glm::atan(glm::tan(glm::radians(45.0f)) * static_cast<float>(viewportHeight) / static_cast<float>(viewportWidth));
            }

            const ProbeFaceOrientation& face = kProbeFaceOrientations[currentFace];
            overrideView = Camera::BuildPerspectiveView(capturePosition, face.forward, face.up, aspect, fovY, state->projectConfig.editorCameraNearPlane);
            bViewOverrideActive = true;

            state->pendingCacheReset = Core::RenderCacheReset::ScreenHistory;
            settleCounter = 0;
            phase = Phase::Settling;
            return;
        }
        case Phase::Settling:
        {
            ++settleCounter;
            if (settleCounter >= settleFrames) {
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
            state->lighting.reflection = stashedReflectionConfig;
            ClearProbeBakeHideSet(ctx, state);
            state->inputContext = stashedInputContext;
            bViewOverrideActive = false;
            bBakeActive = false;
            bFacesReady = true;

            if (captureSize > 0) {
                Core::Path bakeDir = Platform::GetUserDataPath() / "screenshots";
                Platform::CreateDirectories(bakeDir.c_str());
                for (uint32_t face = 0; face < 6; ++face) {
                    if (!faceBuffers[face].IsAllocated()) { continue; }
                    Core::InlineString<64> faceName = Core::InlineString<64>::Format("probe_bake_face_%u.hdr", face);
                    Core::Path facePath = bakeDir / faceName.c_str();
                    WriteProbeFaceHdr(ctx, faceBuffers[face].Data(), captureSize, facePath);
                }
            }

            phase = Phase::Idle;
            return;
        }
    }
}
} // Game
