//
// Created by William on 2026-08-01.
//

#include "capture_shot_system.h"

#include <fstream>

#include <json/nlohmann/json.hpp>

#include "ddgi_converge_boost.h"
#include "probe_bake_system.h"
#include "core/math/constants.h"
#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/logging/engine_log.h"
#include "platform/paths.h"
#include "game/components/core_components.h"
#include "game/components/camera_components.h"
#include "game/components/physics/physics_components.h"
#include "game/components/render/module_mesh_component.h"
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/reflection_probe_component.h"
#include "game/components/render/spline_mesh_component.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/static_mesh_primitive_component.h"
#include "game/components/render/text3d_component.h"
#include "game/gameplay/camera/gameplay_camera.h"

namespace Game
{
static constexpr int32_t READY_QUIET_FRAMES = 30;
static constexpr int32_t READY_TIMEOUT_FRAMES = 3000;
static constexpr int32_t SAVE_TIMEOUT_FRAMES = 300;

static size_t CountLoadingEntities(Engine::EngineState* state)
{
    return state->registry.view<Component::StaticMeshLoadingTag>().size() +
           state->registry.view<Component::StaticMeshPrimitiveLoadingTag>().size() +
           state->registry.view<Component::ProceduralMeshLoadingTag>().size() +
           state->registry.view<Component::SplineMeshLoadingTag>().size() +
           state->registry.view<Component::ModuleMeshLoadingTag>().size() +
           state->registry.view<Component::Text3DLoadingTag>().size() +
           state->registry.view<Component::ReflectionProbeLoadingTag>().size() +
           state->registry.view<Component::PhysicsMeshLoadingTag>().size();
}

static CaptureShotSystem& CaptureShotGetOrCreate(Engine::EngineState* state)
{
    if (auto* existing = state->registry.ctx().find<CaptureShotSystem>()) {
        return *existing;
    }
    return state->registry.ctx().emplace<CaptureShotSystem>();
}

static bool LoadShotList(const char* path, Core::InlineVector<CaptureShotSystem::Shot, 64>& outShots)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR(Game, "Capture run: cannot open shot list '{}'", path);
        return false;
    }
    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.contains("shots") || !j["shots"].is_array()) {
        LOG_ERROR(Game, "Capture run: '{}' is not a valid shot list (expected {{\"shots\": [...]}})", path);
        return false;
    }
    for (const auto& e : j["shots"]) {
        if (outShots.IsFull()) {
            LOG_WARN(Game, "Capture run: shot list truncated to {}", outShots.Size());
            break;
        }
        if (!e.contains("name") || !e["name"].is_string() ||
            !e.contains("translation") || !e["translation"].is_array() || e["translation"].size() != 3 ||
            !e.contains("rotation") || !e["rotation"].is_array() || e["rotation"].size() != 4) {
            LOG_ERROR(Game, "Capture run: malformed shot entry in '{}' (need name, translation[3], rotation[wxyz])", path);
            return false;
        }
        CaptureShotSystem::Shot shot{};
        shot.name = Core::InlineString<64>(e["name"].get<std::string_view>());
        shot.translation = {e["translation"][0].get<float>(), e["translation"][1].get<float>(), e["translation"][2].get<float>()};
        shot.rotation = glm::quat(e["rotation"][0].get<float>(), e["rotation"][1].get<float>(), e["rotation"][2].get<float>(), e["rotation"][3].get<float>());
        if (e.contains("settleFrames") && e["settleFrames"].is_number_integer()) {
            shot.settleFrames = e["settleFrames"].get<int32_t>();
        }
        outShots.PushBack(shot);
    }
    return true;
}

void CaptureShotSystem::Tick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->screenshotPath = {};

    if (bDone || !state->automation.IsCaptureRun()) {
        return;
    }

    auto finish = [&](const char* outcome) {
        if (bActive) {
            state->inputContext = stashedInputContext;
        }
        LOG_INFO(Game, "Capture run {}: {}/{} shot(s) -> {}", outcome, currentShot, static_cast<int32_t>(shots.Size()), outputDir.c_str());
        bActive = false;
        bDone = true;
        phase = Phase::Idle;
        if (state->automation.bExitWhenDone) {
            state->bRequestedQuit = true;
        }
    };

    if (!bActive) {
        if (ProbeBakeActive(state)) {
            return;
        }
        if (!LoadShotList(state->automation.shotsPath.c_str(), shots) || shots.IsEmpty()) {
            bDone = true;
            if (state->automation.bExitWhenDone) {
                state->bRequestedQuit = true;
            }
            return;
        }
        outputDir = state->automation.outputDir;
        if (outputDir.IsEmpty()) {
            outputDir = Core::InlineString<512>((Platform::GetUserDataPath() / "captures").c_str());
        }
        settleFrames = state->automation.settleFrames > 0 ? state->automation.settleFrames : glm::max(1, state->projectConfig.probeBake.settleFrames);
        // Console restores via its own prevContext once we steal the context (same guard as ProbeBakeSystem)
        stashedInputContext = state->inputContext == Engine::InputContext::Console ? Engine::InputContext::Editor : state->inputContext;
        state->inputContext = Engine::InputContext::ProbeBake;
        bActive = true;
        currentShot = 0;
        readyQuietCounter = 0;
        readyWaitedFrames = 0;
        phase = Phase::WaitReady;
        LOG_INFO(Game, "Capture run: {} shot(s), settle {} frame(s), out {}", static_cast<int32_t>(shots.Size()), settleFrames, outputDir.c_str());
        return;
    }

    switch (phase) {
        case Phase::Idle:
        {
            return;
        }
        case Phase::WaitReady:
        {
            const size_t loadingEntities = CountLoadingEntities(state);
            const bool bQuiet = !state->bPendingModelResolve && loadingEntities == 0 &&
                                !ctx->bShouldRescanResources && !ctx->bAssetGenerationPending && !ctx->assetManager->HasPendingLoads();
            readyQuietCounter = bQuiet ? readyQuietCounter + 1 : 0;
            ++readyWaitedFrames;
            if (readyQuietCounter >= READY_QUIET_FRAMES) {
                phase = Phase::ShotSetup;
            }
            else if (readyWaitedFrames >= READY_TIMEOUT_FRAMES) {
                LOG_WARN(Game, "Capture run: asset readiness timed out after {} frames; capturing anyway. Gates: pendingModelResolve={} loadingEntities={} rescan={} generation={} pendingLoads={}",
                         readyWaitedFrames, state->bPendingModelResolve, loadingEntities,
                         ctx->bShouldRescanResources, ctx->bAssetGenerationPending, ctx->assetManager->HasPendingLoads());
                ctx->assetManager->LogPendingLoads();
                phase = Phase::ShotSetup;
            }
            return;
        }
        case Phase::ShotSetup:
        {
            auto camView = state->registry.view<Component::EditorCameraTag, Component::CameraComponent, Component::TransformComponent>();
            const entt::entity camEntity = camView.front();
            if (camEntity == entt::null) {
                LOG_ERROR(Game, "Capture run: no editor camera entity");
                finish("aborted");
                return;
            }
            const Shot& shot = shots[currentShot];
            auto [camera, transform] = camView.get<Component::CameraComponent, Component::TransformComponent>(camEntity);
            transform.translation = shot.translation;
            transform.rotation = shot.rotation;
            const float aspect = static_cast<float>(ctx->windowContext.viewportWidth) / static_cast<float>(ctx->windowContext.viewportHeight);
            camera.currentViewData = Camera::BuildPerspectiveView(shot.translation, shot.rotation * WORLD_FORWARD, WORLD_UP, aspect,
                                                                  glm::radians(state->projectConfig.editorCameraFovDegrees), state->projectConfig.editorCameraNearPlane);
            camera.previousViewData = camera.currentViewData;

            if (state->pendingCacheReset == Core::RenderCacheReset::None) {
                state->pendingCacheReset = Core::RenderCacheReset::ScreenHistory;
            }
            if (currentShot == 0 && state->projectConfig.probeBake.bAutoConverge) {
                DDGIConvergeBoostTrigger(state->ddgiConvergeBoost, state->lighting.ddgi);
            }
            settleCounter = 0;
            phase = Phase::Settling;
            return;
        }
        case Phase::Settling:
        {
            ++settleCounter;
            const int32_t target = shots[currentShot].settleFrames > 0 ? shots[currentShot].settleFrames : settleFrames;
            if (settleCounter >= target) {
                if (currentShot == 0 && state->ddgiConvergeBoost.bActive) {
                    return;
                }
                phase = Phase::CaptureRequested;
            }
            return;
        }
        case Phase::CaptureRequested:
        {
            if (ctx->bScreenshotInFlight) {
                return;
            }
            const Core::Path savePath = Core::Path(outputDir.c_str()) / Core::InlineString<128>::Format("%s.png", shots[currentShot].name.c_str()).c_str();
            frameBuffer->screenshotPath = Core::InlineString<512>(savePath.c_str());
            state->bWantsScreenshot = true;
            bSawInFlight = false;
            awaitFrames = 0;
            phase = Phase::AwaitSaved;
            return;
        }
        case Phase::AwaitSaved:
        {
            bSawInFlight = bSawInFlight || ctx->bScreenshotInFlight;
            ++awaitFrames;
            const bool bSaved = bSawInFlight && !ctx->bScreenshotInFlight;
            if (!bSaved && awaitFrames < SAVE_TIMEOUT_FRAMES) {
                return;
            }
            if (!bSaved) {
                LOG_WARN(Game, "Capture run: shot '{}' save not observed after {} frames", shots[currentShot].name.c_str(), awaitFrames);
            }
            ++currentShot;
            if (currentShot >= static_cast<int32_t>(shots.Size())) {
                finish("complete");
            }
            else {
                phase = Phase::ShotSetup;
            }
            return;
        }
    }
}

void CaptureShotTick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    CaptureShotGetOrCreate(state).Tick(ctx, state, frameBuffer);
}

void CaptureShotScrubFrame(Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    const auto* system = state->registry.ctx().find<CaptureShotSystem>();
    if (system == nullptr || !system->bActive) { return; }

    Core::ViewFamily& viewFamily = frameBuffer->mainViewFamily;
    viewFamily.debugLines.Clear();
    viewFamily.debugBoxes.Clear();
    viewFamily.debugSpheres.Clear();
    viewFamily.debugRects.Clear();
    viewFamily.debugArrows.Clear();
    viewFamily.debugCapsules.Clear();
    viewFamily.debugCylinders.Clear();
    viewFamily.sprites.Clear();
    viewFamily.probePreviews.Clear();

    frameBuffer->selectedStableId = 0;
    frameBuffer->bEnableGPUDebug = false;
}
} // Game
