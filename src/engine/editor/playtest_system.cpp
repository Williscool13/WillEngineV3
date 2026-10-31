//
// Created by William on 2026-09-09.
//

#include "playtest_system.h"

#include <algorithm>
#include <chrono>
#include <cstring>
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
#include "engine/serialization/text_writer.h"
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

size_t CountLoadingEntities(Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    return registry.View<Component::StaticMeshLoadingFlag>().size() +
           registry.View<Component::StaticMeshPrimitiveLoadingFlag>().size() +
           registry.View<Component::ProceduralMeshLoadingFlag>().size() +
           registry.View<Component::SplineMeshLoadingFlag>().size() +
           registry.View<Component::ModuleMeshLoadingFlag>().size() +
           registry.View<Component::Text3DLoadingFlag>().size() +
           registry.View<Component::ReflectionProbeLoadingFlag>().size() +
           registry.View<Component::PhysicsMeshLoadingFlag>().size() +
           registry.View<Component::StaticMeshLoadRequest>().size() +
           registry.View<Component::StaticMeshPrimitiveLoadRequest>().size() +
           registry.View<Component::ProceduralMeshLoadRequest>().size() +
           registry.View<Component::SplineMeshLoadRequest>().size() +
           registry.View<Component::ModuleMeshLoadRequest>().size() +
           registry.View<Component::Text3DGenerateRequest>().size() +
           registry.View<Component::ReflectionProbeLoadRequest>().size() +
           registry.View<Component::PhysicsMeshRequest>().size() +
           registry.View<Component::PhysicsShapeCreationRequest>().size() +
           registry.View<Component::PhysicsBodyCreationRequest>().size() +
           registry.View<Component::LightSurfaceRequest>().size() +
           registry.View<Component::TextFontRequest>().size();
}

static bool LoadPlayFile(const char* path, Core::Vector<PlaytestSystem::Event>& outEvents, Core::InlineString<128>& outName)
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
        if (outEvents.Size() >= PlaytestSystem::MAX_EVENTS) {
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
            ev.bUseRuntimeScenes = e.Bool("useRuntimeScenes", false);
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
static void TeleportEditorCamera(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, const glm::vec3& translation, const glm::quat& rotation, bool bCut = true)
{
    auto camView = registry.View<Component::EditorCameraTag, Engine::Mut<Component::CameraRuntime>, Engine::Mut<Component::TransformComponent>>();
    const entt::entity camEntity = camView.front();
    if (camEntity == entt::null) { return; }
    auto [camera, transform] = camView.get<Component::CameraRuntime, Component::TransformComponent>(camEntity);
    transform.translation = translation;
    transform.rotation = rotation;
    const float aspect = static_cast<float>(ctx->windowContext.viewportWidth) / static_cast<float>(ctx->windowContext.viewportHeight);
    camera.currentViewData = BuildPerspectiveView(translation, rotation * WORLD_FORWARD, WORLD_UP, aspect,
                                                  glm::radians(state->projectConfig.editorCameraFovDegrees), state->projectConfig.editorCameraNearPlane);
    if (bCut) {
        state->requests.RequestRenderReset(Core::RenderReset::Cut);
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

void PlaytestSystem::Tick(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    Core::FrameBuffer& frameBuffer = registry.FrameMut<Core::FrameBuffer>();
    frameBuffer.screenshotPath = {};

    if (!bActive) {
        if (pendingPath.IsEmpty() && !bCliConsumed) {
            bCliConsumed = true;
            const char* playPath = Args::GetString(*state->launchArgs, ARG_PLAY);
            if (playPath[0] != '\0') {
                pendingPath = Core::InlineString<512>(playPath);
                bCliRun = true;
            }
        }
        if (pendingPath.IsEmpty() || ProbeBakeActive(state)) {
            return;
        }

        if (events.IsEmpty()) {
            events = Core::Vector<Event>(&ctx->memoryManager->General());
        }
        events.Clear();
        const Core::InlineString<512> path = pendingPath;
        pendingPath.Clear();
        if (!LoadPlayFile(path.c_str(), events, runName)) {
            if (bCliRun && Args::GetBool(*state->launchArgs, ARG_EXIT)) {
                state->requests.bRequestedQuit = true;
            }
            bCliRun = false;
            return;
        }

        const Core::InlineString<128> stem{Core::Path(path.c_str()).Stem()};
        outputDir = Core::InlineString<512>(Args::GetString(*state->launchArgs, ARG_OUT));
        if (outputDir.IsEmpty()) {
            outputDir = Core::InlineString<512>((Platform::GetUserDataPath() / "screenshots" / stem.c_str() / LocalTimestamp().c_str()).c_str());
        }
        Platform::CreateDirectories(outputDir.c_str());

        // Console restores via its own prevContext once we steal the context (same guard as ProbeBakeSystem)
        stashedInputContext = state->inputContext == Engine::InputContext::Console ? Engine::InputContext::Editor : state->inputContext;
        state->inputContext = Engine::InputContext::ProbeBake; {
            auto camView = registry.View<Component::EditorCameraTag, Component::TransformComponent>();
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
            PlayStop(ctx, state, registry);
        }
        state->input.scripted.Clear();
        cameraOverride = {};
        frameLimit = 0;
        if (bProfileStashed) {
            Profiles::ApplyLightingProfile(*state, stashedProfile);
            state->requests.RequestRenderReset(Core::RenderReset::Flush);
            bProfileStashed = false;
        }
        state->inputContext = stashedInputContext;
        TeleportEditorCamera(ctx, state, registry, stashedCameraTranslation, stashedCameraRotation);
        LOG_INFO(Engine, "Run '{}' {}: {} capture(s) -> {}", runName.c_str(), outcome, captureCount, outputDir.c_str());
        bActive = false;
        phase = Phase::Idle;
        if (bCliRun && Args::GetBool(*state->launchArgs, ARG_EXIT)) {
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
            const size_t loadingEntities = CountLoadingEntities(state, registry);
            const bool bQuiet = loadingEntities == 0 &&
                                !ctx->rescan.bResources && !ctx->frameStatus.bAssetGenerationPending && !registry.Service<Engine::AssetManager>().HasPendingLoads();
            readyQuietCounter = bQuiet ? readyQuietCounter + 1 : 0;
            ++readyWaitedFrames;
            if (readyQuietCounter >= READY_QUIET_FRAMES) {
                phase = Phase::Step;
            }
            else if (readyWaitedFrames >= READY_TIMEOUT_FRAMES) {
                LOG_WARN(Engine, "Run: asset readiness timed out after {} frames; continuing anyway. Gates: loadingEntities={} rescan={} generation={} pendingLoads={}", readyWaitedFrames, loadingEntities,
                         ctx->rescan.bResources, ctx->frameStatus.bAssetGenerationPending, registry.Service<Engine::AssetManager>().HasPendingLoads());
                registry.Service<Engine::AssetManager>().LogPendingLoads();
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
                            TeleportEditorCamera(ctx, state, registry, ov.translation, ov.rotation, e.bFlag);
                        }
                        else {
                            ov.mode = e.camMode == CamMode::Held ? CameraOverride::Mode::Held : CameraOverride::Mode::Track;
                            ov.translation = e.translation;
                            ov.rotation = e.rotation;
                            TeleportEditorCamera(ctx, state, registry, e.translation, e.rotation, e.bFlag);
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
                            if (!IsPlaying(state)) { PlayStart(ctx, state, registry, e.bUseRuntimeScenes); }
                        }
                        else if (IsPlaying(state)) {
                            PlayStop(ctx, state, registry);
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
                        state->requests.RequestRenderReset(Core::RenderReset::Cut);
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
                            state->requests.RequestRenderReset(Core::RenderReset::Flush);
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

void PlaytestTick(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    state->playtest.Tick(ctx, state, registry);
}

bool CameraRecorder::Start(Engine::EngineContext* ctx, Engine::EngineState* state, const char* recordingName)
{
    if (bActive || state->playtest.bActive || recordingName == nullptr || recordingName[0] == '\0') {
        return false;
    }
    samples = Core::Vector<Sample>(&ctx->memoryManager->General());
    name = Core::InlineString<128>(std::string_view(recordingName));
    startTime = std::chrono::steady_clock::now();
    bActive = true;
    return true;
}

void CameraRecorder::Tick(Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    if (!bActive) {
        return;
    }
    auto camView = registry.View<Component::EditorCameraTag, Component::TransformComponent>();
    const entt::entity camEntity = camView.front();
    if (camEntity == entt::null) {
        return;
    }
    const auto& transform = camView.get<Component::TransformComponent>(camEntity);
    samples.PushBack(Sample{.translation = transform.translation, .rotation = transform.rotation});
}

Core::InlineString<512> CameraRecorder::Stop(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    if (!bActive) {
        return {};
    }
    bActive = false;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
    if (samples.IsEmpty() || seconds <= 0.0) {
        return {};
    }
    const int32_t fps = glm::max(1, static_cast<int32_t>(glm::round(static_cast<double>(samples.Size()) / seconds)));
    const auto* sceneMeta = registry.Assets().GetSceneMetadata(state->scene.currentSceneId);
    const Core::Path folder = sceneMeta ? sceneMeta->source.Parent() : Platform::GetScenePath();
    const Core::Path path = folder / Core::InlineString<160>::Format("%s.wplay", name.c_str()).c_str();

    WPlayHeader header{};
    memcpy(header.name, name.c_str(), std::min(name.Size(), WPLAY_NAME_LENGTH - 1));
    header.sceneId = state->scene.currentSceneId.id;
    header.contentVersion = 1;
    if (Platform::FileExists(path)) {
        if (const auto existing = ReadWPlayHeader(path)) {
            header.contentVersion = existing->contentVersion + 1;
        }
    }
    header.eventCount = 1u + 2u * static_cast<uint32_t>(samples.Size());

    Core::Vector<std::byte> out(&ctx->memoryManager->AssetsScratch());
    WriteWPlayHeader(out, header);
    Engine::TextWriter w(out);
    w.Count("events", header.eventCount);
    w.BeginBlock("e");
    w.Key("fps", fps);
    w.EndBlock();
    for (size_t i = 0; i < samples.Size(); ++i) {
        w.BeginBlock("e");
        w.KeyStr("cam", "held");
        w.Key("translation", samples[i].translation);
        w.Key("rotation", samples[i].rotation);
        if (i != 0) {
            w.Key("cut", 0);
        }
        w.EndBlock();
        w.BeginBlock("e");
        w.Key("wait", 1);
        w.EndBlock();
    }
    if (!Platform::WriteFile(path, out.Data(), out.Size())) {
        LOG_ERROR(Engine, "Record: failed to write '{}'", path.c_str());
        return {};
    }
    ctx->rescan.bResources = true;
    LOG_INFO(Engine, "Record '{}': {} frames at {} fps -> {}", name.c_str(), static_cast<int32_t>(samples.Size()), fps, path.c_str());
    return Core::InlineString<512>(std::string_view(path.c_str()));
}

void CameraRecordTick(Engine::EngineContext*, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    state->cameraRecorder.Tick(state, registry);
}

void PlaytestScrubFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    Core::FrameBuffer& frameBuffer = registry.FrameMut<Core::FrameBuffer>();
    if (!state->playtest.bActive) { return; }

    Core::ViewFamily& viewFamily = registry.FrameMut<Core::ViewFamily>();
    viewFamily.debugDraws.debugLines.Clear();
    viewFamily.debugDraws.debugBoxes.Clear();
    viewFamily.debugDraws.debugSpheres.Clear();
    viewFamily.debugDraws.debugRects.Clear();
    viewFamily.debugDraws.debugArrows.Clear();
    viewFamily.debugDraws.debugCapsules.Clear();
    viewFamily.debugDraws.debugCylinders.Clear();
    viewFamily.spriteDraws.sprites.Clear();
    viewFamily.probes.probePreviews.Clear();

    frameBuffer.selectedStableId = 0;
    frameBuffer.debug.bEnableGPUDebug = false;
}
} // Engine
