//
// Created by William on 2026-09-09.
//

#include "playtest_system.h"

#include <chrono>
#include <ctime>

#include "ddgi_converge_boost.h"
#include "probe_bake_system.h"
#include "core/math/constants.h"
#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/scene/play_format.h"
#include "engine/serialization/text_reader.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "engine/components/core_components.h"
#include "engine/components/camera_components.h"
#include "engine/components/physics/physics_components.h"
#include "engine/components/render/light_components.h"
#include "engine/components/render/module_mesh_component.h"
#include "engine/components/render/procedural_mesh_component.h"
#include "engine/components/render/reflection_probe_component.h"
#include "engine/components/render/spline_mesh_component.h"
#include "engine/components/render/static_mesh_component.h"
#include "engine/components/render/static_mesh_primitive_component.h"
#include "engine/components/render/text3d_component.h"
#include "engine/components/render/text_component.h"
#include "engine/systems/camera_system.h"
#include "engine/systems/scene_system.h"

namespace Engine
{
static constexpr int32_t READY_QUIET_FRAMES = 30;
static constexpr int32_t READY_TIMEOUT_FRAMES = 3000;
static constexpr int32_t SAVE_TIMEOUT_FRAMES = 300;

size_t CountLoadingEntities(Engine::EngineState* state)
{
    return state->registry.view<Component::StaticMeshLoadingTag>().size() +
           state->registry.view<Component::StaticMeshPrimitiveLoadingTag>().size() +
           state->registry.view<Component::ProceduralMeshLoadingTag>().size() +
           state->registry.view<Component::SplineMeshLoadingTag>().size() +
           state->registry.view<Component::ModuleMeshLoadingTag>().size() +
           state->registry.view<Component::Text3DLoadingTag>().size() +
           state->registry.view<Component::ReflectionProbeLoadingTag>().size() +
           state->registry.view<Component::PhysicsMeshLoadingTag>().size() +
           state->registry.view<Component::StaticMeshLoadPendingTag>().size() +
           state->registry.view<Component::StaticMeshPrimitiveLoadPendingTag>().size() +
           state->registry.view<Component::ProceduralMeshLoadPendingTag>().size() +
           state->registry.view<Component::SplineMeshLoadPendingTag>().size() +
           state->registry.view<Component::ModuleMeshLoadPendingTag>().size() +
           state->registry.view<Component::Text3DGeneratePendingTag>().size() +
           state->registry.view<Component::ReflectionProbeLoadPendingTag>().size() +
           state->registry.view<Component::PendingPhysicsMeshTag>().size() +
           state->registry.view<Component::PendingPhysicsShapeCreationTag>().size() +
           state->registry.view<Component::PendingPhysicsBodyCreationTag>().size() +
           state->registry.view<Component::LightSurfacePendingTag>().size() +
           state->registry.view<Component::TextFontPendingTag>().size();
}

static bool LoadPlayFile(const char* path, Core::InlineVector<PlaytestSystem::Event, PlaytestSystem::MAX_EVENTS>& outEvents, Core::InlineString<128>& outName)
{
    Platform::ScopedFileMapping map{Core::Path(path)};
    if (!map.data) {
        LOG_ERROR(Engine, "Run: cannot open '{}'", path);
        return false;
    }
    const auto header = ReadWPlayHeader(map.data, map.size);
    if (!header) {
        LOG_ERROR(Engine, "Run: '{}' is not a .wplay (bad magic or major version)", path);
        return false;
    }
    outName = Core::InlineString<128>(header->name);

    const Engine::TextReader r(map.data + header->dataOffset, map.size - header->dataOffset);
    r.ForEachRecord("events", [&](const Engine::TextReader& e) {
        if (outEvents.IsFull()) {
            LOG_WARN(Engine, "Run: '{}' truncated to {} events", path, outEvents.Size());
            return;
        }
        PlaytestSystem::Event ev{};
        Core::InlineString<16> cam{};
        Core::InlineString<64> action{};
        if (e.Str("cam", cam)) {
            ev.op = PlaytestSystem::Op::Cam;
            ev.camMode = cam == "held" ? PlaytestSystem::CamMode::Held : cam == "track" ? PlaytestSystem::CamMode::Track : cam == "preset" ? PlaytestSystem::CamMode::Preset : PlaytestSystem::CamMode::Follow;
            if (ev.camMode == PlaytestSystem::CamMode::Preset) {
                ev.count = e.Int("preset");
                ev.translation = e.Has("offset") ? e.Vec3("offset") : glm::vec3(0.0f);
            }
            else {
                ev.translation = e.Vec3("translation");
                ev.rotation = e.Quat("rotation");
            }
            ev.bFlag = e.Bool("cut", true);
        }
        else if (e.Has("play")) {
            ev.op = PlaytestSystem::Op::Play;
            ev.bFlag = e.Bool("play");
        }
        else if (e.Str("action", action)) {
            ev.action = ActionHandle{StringID(action.c_str(), action.Size()).id};
            if (e.Has("axis")) {
                ev.op = PlaytestSystem::Op::Axis;
                ev.axis = e.Vec2("axis");
            }
            else {
                ev.op = PlaytestSystem::Op::Button;
                ev.bFlag = e.Bool("down");
            }
        }
        else if (e.Has("wait")) {
            ev.op = PlaytestSystem::Op::Wait;
            ev.count = e.Int("wait");
        }
        else if (e.Has("reset")) {
            ev.op = PlaytestSystem::Op::Reset;
        }
        else if (e.Str("capture", ev.name)) {
            ev.op = PlaytestSystem::Op::Capture;
            ev.count = glm::max(1, e.Int("frames", 1));
            ev.fps = e.Int("fps", 0);
        }
        else if (e.Has("fps")) {
            ev.op = PlaytestSystem::Op::Fps;
            ev.fps = e.Int("fps");
        }
        else if (e.Str("console", ev.name)) {
            ev.op = PlaytestSystem::Op::Console;
        }
        else if (e.Str("profile", ev.name)) {
            ev.op = PlaytestSystem::Op::Profile;
        }
        else {
            LOG_ERROR(Engine, "Run: '{}' has an event with no recognised op, skipped", path);
            return;
        }
        outEvents.PushBack(ev);
    });
    if (outEvents.IsEmpty()) {
        LOG_ERROR(Engine, "Run: '{}' has no events", path);
        return false;
    }
    return true;
}

/** bCut drops last frame's view so the move reads as a cut (zero camera motion vectors); false keeps it, so a cam event per frame renders as real camera motion. */
static void TeleportEditorCamera(Engine::EngineContext* ctx, Engine::EngineState* state, const glm::vec3& translation, const glm::quat& rotation, bool bCut = true)
{
    auto camView = state->registry.view<Component::EditorCameraTag, Component::CameraComponent, Component::TransformComponent>();
    const entt::entity camEntity = camView.front();
    if (camEntity == entt::null) { return; }
    auto [camera, transform] = camView.get<Component::CameraComponent, Component::TransformComponent>(camEntity);
    transform.translation = translation;
    transform.rotation = rotation;
    const float aspect = static_cast<float>(ctx->windowContext.viewportWidth) / static_cast<float>(ctx->windowContext.viewportHeight);
    camera.currentViewData = BuildPerspectiveView(translation, rotation * WORLD_FORWARD, WORLD_UP, aspect,
                                                  glm::radians(state->projectConfig.editorCameraFovDegrees), state->projectConfig.editorCameraNearPlane);
    if (bCut) {
        camera.previousViewData = camera.currentViewData;
    }
}

static void SetScriptedAction(Engine::InputState& input, ActionHandle action, const glm::vec2& axis, bool bDown)
{
    for (ScriptedAction& s : input.scripted) {
        if (s.action == action) {
            s.axis = axis;
            s.bDown = bDown;
            return;
        }
    }
    if (input.scripted.IsFull()) {
        LOG_WARN(Engine, "Run: scripted action table is full ({}), action ignored", input.scripted.Size());
        return;
    }
    input.scripted.PushBack(ScriptedAction{.action = action, .axis = axis, .bDown = bDown, .bWasDown = false});
}

static Core::InlineString<32> LocalTimestamp()
{
    const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm;
    localtime_s(&tm, &time);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return Core::InlineString<32>(buf);
}

void PlaytestSystem::Tick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->screenshotPath = {};

    if (!bActive) {
        if (pendingPath.IsEmpty() && !bCliConsumed && state->automation.IsPlayRun()) {
            pendingPath = state->automation.playPath;
            bCliConsumed = true;
            bCliRun = true;
        }
        if (pendingPath.IsEmpty() || ProbeBakeActive(state)) {
            return;
        }

        events.Clear();
        const Core::InlineString<512> path = pendingPath;
        pendingPath.Clear();
        if (!LoadPlayFile(path.c_str(), events, runName)) {
            if (bCliRun && state->automation.bExitWhenDone) {
                state->requests.bRequestedQuit = true;
            }
            bCliRun = false;
            return;
        }

        const Core::InlineString<128> stem{Core::Path(path.c_str()).Stem()};
        outputDir = state->automation.outputDir;
        if (outputDir.IsEmpty()) {
            outputDir = Core::InlineString<512>((Platform::GetUserDataPath() / "screenshots" / stem.c_str() / LocalTimestamp().c_str()).c_str());
        }
        Platform::CreateDirectories(outputDir.c_str());

        // Console restores via its own prevContext once we steal the context (same guard as ProbeBakeSystem)
        stashedInputContext = state->inputContext == Engine::InputContext::Console ? Engine::InputContext::Editor : state->inputContext;
        state->inputContext = Engine::InputContext::ProbeBake; {
            auto camView = state->registry.view<Component::EditorCameraTag, Component::TransformComponent>();
            const entt::entity camEntity = camView.front();
            if (camEntity != entt::null) {
                const auto& transform = camView.get<Component::TransformComponent>(camEntity);
                stashedCameraTranslation = transform.translation;
                stashedCameraRotation = transform.rotation;
            }
        }
        bool bHasPlay = false;
        for (const Event& e : events) {
            bHasPlay = bHasPlay || e.op == Op::Play;
        }
        if (state->projectConfig.probeBake.bAutoConverge && !bHasPlay) {
            DDGIConvergeBoostTrigger(state->ddgiConvergeBoost, state->lighting.ddgi);
        }

        bActive = true;
        cursor = 0;
        captureCount = 0;
        readyQuietCounter = 0;
        readyWaitedFrames = 0;
        phase = Phase::WaitReady;
        LOG_INFO(Engine, "Run '{}': {} event(s), out {}", runName.c_str(), static_cast<int32_t>(events.Size()), outputDir.c_str());
        return;
    }

    auto finish = [&](const char* outcome) {
        if (IsPlaying(state)) {
            PlayStop(ctx, state);
        }
        state->input.scripted.Clear();
        cameraOverride = {};
        frameLimit = 0;
        if (bProfileStashed) {
            Profiles::ApplyLightingProfile(*state, stashedProfile);
            state->requests.pendingCacheReset = Core::RenderCacheReset::All;
            bProfileStashed = false;
        }
        state->inputContext = stashedInputContext;
        TeleportEditorCamera(ctx, state, stashedCameraTranslation, stashedCameraRotation);
        LOG_INFO(Engine, "Run '{}' {}: {} capture(s) -> {}", runName.c_str(), outcome, captureCount, outputDir.c_str());
        bActive = false;
        phase = Phase::Idle;
        if (bCliRun && state->automation.bExitWhenDone) {
            state->requests.bRequestedQuit = true;
        }
        bCliRun = false;
    };

    switch (phase) {
        case Phase::Idle:
        {
            return;
        }
        case Phase::WaitReady:
        {
            const size_t loadingEntities = CountLoadingEntities(state);
            const bool bQuiet = loadingEntities == 0 &&
                                !ctx->rescan.bResources && !ctx->frameStatus.bAssetGenerationPending && !ctx->assetManager->HasPendingLoads();
            readyQuietCounter = bQuiet ? readyQuietCounter + 1 : 0;
            ++readyWaitedFrames;
            if (readyQuietCounter >= READY_QUIET_FRAMES) {
                phase = Phase::Step;
            }
            else if (readyWaitedFrames >= READY_TIMEOUT_FRAMES) {
                LOG_WARN(Engine, "Run: asset readiness timed out after {} frames; continuing anyway. Gates: loadingEntities={} rescan={} generation={} pendingLoads={}", readyWaitedFrames, loadingEntities,
                         ctx->rescan.bResources, ctx->frameStatus.bAssetGenerationPending, ctx->assetManager->HasPendingLoads());
                ctx->assetManager->LogPendingLoads();
                phase = Phase::Step;
            }
            return;
        }
        case Phase::Step:
        {
            while (cursor < static_cast<int32_t>(events.Size())) {
                const Event& e = events[cursor];
                switch (e.op) {
                    case Op::Cam:
                    {
                        CameraOverride& ov = cameraOverride;
                        if (e.camMode == CamMode::Follow) {
                            ov = {};
                        }
                        else if (e.camMode == CamMode::Preset) {
                            if (e.count < 1 || e.count > MAX_CAMERA_PRESETS || !state->projectConfig.cameraPresets[e.count - 1].bSet) {
                                finish("aborted: cam preset slot is out of range or empty");
                                return;
                            }
                            const CameraPreset& preset = state->projectConfig.cameraPresets[e.count - 1];
                            ov.mode = CameraOverride::Mode::Held;
                            ov.rotation = preset.rotation;
                            ov.translation = preset.translation + preset.rotation * e.translation;
                            TeleportEditorCamera(ctx, state, ov.translation, ov.rotation, e.bFlag);
                        }
                        else {
                            ov.mode = e.camMode == CamMode::Held ? CameraOverride::Mode::Held : CameraOverride::Mode::Track;
                            ov.translation = e.translation;
                            ov.rotation = e.rotation;
                            TeleportEditorCamera(ctx, state, e.translation, e.rotation, e.bFlag);
                        }
                        ++cursor;
                        break;
                    }
                    case Op::Play:
                    {
                        if (e.bFlag) {
                            if (!ctx->bGameLoaded) {
                                finish("aborted: game.dll is not loaded");
                                return;
                            }
                            if (!IsPlaying(state)) { PlayStart(ctx, state); }
                        }
                        else if (IsPlaying(state)) {
                            PlayStop(ctx, state);
                            state->inputContext = Engine::InputContext::ProbeBake;
                        }
                        ++cursor;
                        break;
                    }
                    case Op::Axis:
                    {
                        SetScriptedAction(state->input, e.action, e.axis, false);
                        ++cursor;
                        break;
                    }
                    case Op::Button:
                    {
                        SetScriptedAction(state->input, e.action, glm::vec2(0.0f), e.bFlag);
                        ++cursor;
                        break;
                    }
                    case Op::Wait:
                    {
                        waitCounter = 0;
                        waitStepBase = state->physics.stepCount;
                        ++cursor;
                        phase = Phase::Waiting;
                        return;
                    }
                    case Op::Reset:
                    {
                        if (state->requests.pendingCacheReset == Core::RenderCacheReset::None) {
                            state->requests.pendingCacheReset = Core::RenderCacheReset::ScreenHistory;
                        }
                        ++cursor;
                        break;
                    }
                    case Op::Capture:
                    {
                        if (bSkipCaptures) {
                            ++cursor;
                            break;
                        }
                        if (ctx->frameStatus.bScreenshotInFlight || state->requests.bWantsScreenshot || state->requests.screenshotBurstRemaining > 0) {
                            return;
                        }
                        const Core::Path base = Core::Path(outputDir.c_str()) / e.name.c_str();
                        if (e.count > 1) {
                            state->requests.screenshotBurstBase = Core::InlineString<512>(base.c_str());
                            state->requests.screenshotBurstRemaining = e.count;
                            state->requests.screenshotBurstIndex = 0;
                        }
                        else {
                            state->requests.screenshotPath = Core::InlineString<512>::Format("%s.png", base.c_str());
                            state->requests.bWantsScreenshot = true;
                        }
                        bSawInFlight = false;
                        awaitFrames = 0;
                        if (e.fps > 0) {
                            frameLimit = e.fps;
                        }
                        ++cursor;
                        phase = Phase::Capturing;
                        return;
                    }
                    case Op::Fps:
                    {
                        fpsCap = glm::max(0, e.fps);
                        frameLimit = fpsCap;
                        ++cursor;
                        break;
                    }
                    case Op::Console:
                    {
                        Console::ExecuteCommand(ctx, state, e.name.c_str());
                        ++cursor;
                        break;
                    }
                    case Op::Profile:
                    {
                        if (!bProfileStashed) {
                            stashedProfile = Profiles::CaptureLightingProfile(*state);
                            bProfileStashed = true;
                        }
                        Profiles::LightingProfileBundle bundle = Profiles::CaptureLightingProfile(*state);
                        if (Profiles::LoadLightingProfile(e.name.c_str(), bundle)) {
                            bundle.gtao.bEnabled = state->lighting.gtaoConfig.bEnabled;
                            Profiles::ApplyLightingProfile(*state, bundle);
                            state->requests.pendingCacheReset = Core::RenderCacheReset::All;
                        }
                        else {
                            LOG_WARN(Engine, "Run '{}': lighting profile '{}' not found", runName.c_str(), e.name.c_str());
                        }
                        ++cursor;
                        break;
                    }
                }
            }
            finish("complete");
            return;
        }
        case Phase::Capturing:
        {
            if (state->requests.screenshotBurstRemaining > 0) {
                return;
            }
            phase = Phase::AwaitSaved;
            return;
        }
        case Phase::Waiting:
        {
            if (state->ddgiConvergeBoost.bActive) {
                return;
            }
            const int32_t target = events[cursor - 1].count;
            const bool bStepping = IsPlaying(state) && state->inputContext == Engine::InputContext::Gameplay && state->physics.bEnabled;
            const bool bElapsed = bStepping ? (state->physics.stepCount - waitStepBase) >= static_cast<uint64_t>(glm::max(target, 0)) : ++waitCounter >= target;
            if (bElapsed) {
                phase = Phase::Step;
            }
            return;
        }
        case Phase::AwaitSaved:
        {
            bSawInFlight = bSawInFlight || ctx->frameStatus.bScreenshotInFlight;
            ++awaitFrames;
            const bool bSaved = bSawInFlight && !ctx->frameStatus.bScreenshotInFlight;
            if (!bSaved && awaitFrames < SAVE_TIMEOUT_FRAMES) {
                return;
            }
            if (!bSaved) {
                LOG_WARN(Engine, "Run: capture '{}' save not observed after {} frames", events[cursor - 1].name.c_str(), awaitFrames);
            }
            else {
                captureCount += glm::max(events[cursor - 1].count, 1);
            }
            frameLimit = fpsCap;
            phase = Phase::Step;
            return;
        }
    }
}

void PlaytestTick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    state->playtest.Tick(ctx, state, frameBuffer);
}

void PlaytestScrubFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    if (!state->playtest.bActive) { return; }

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
    frameBuffer->debug.bEnableGPUDebug = false;
}
} // Engine
