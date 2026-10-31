//
// Created by William on 2026-09-05.
//

#include "engine_lifecycle.h"

#include "clay/clay.h"

#include "core/math/constants.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/logging/engine_log.h"
#include "engine/components/camera_components.h"
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/resources/font/font_metrics.h"
#include "engine/resources/scene/play_format.h"
#include "engine/systems/scene_system.h"

namespace Engine
{
static constexpr Vec3 DEFAULT_CAMERA_POS{0.0f, 3.0f, 5.0f};

void CreateCameras(EngineState* state, Vec3 editorPos, Quat editorRot, SystemRegistry& registry)
{
    const entt::entity editorCamera = registry.Create();
    registry.Add<Component::FreeCameraComponent>(editorCamera);
    registry.Add<Component::CameraRuntime>(editorCamera);
    registry.Add<Component::EditorCameraTag>(editorCamera);
    auto& editorCameraTransform = registry.Add<Component::TransformComponent>(editorCamera);
    editorCameraTransform.translation = editorPos;
    editorCameraTransform.rotation = editorRot;

    const entt::entity gameCamera = registry.Create();
    registry.Add<Component::FreeCameraComponent>(gameCamera);
    registry.Add<Component::CameraRuntime>(gameCamera);
    registry.Add<Component::GameCameraTag>(gameCamera);
    auto& gameCameraTransform = registry.Add<Component::TransformComponent>(gameCamera);
    gameCameraTransform.translation = DEFAULT_CAMERA_POS;
    gameCameraTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.0f) - DEFAULT_CAMERA_POS), WORLD_UP);
}

void CreateDefaultCameras(EngineState* state)
{
    Engine::SystemRegistry registry{state->registry, Engine::AccessSet::EXCLUSIVE, nullptr, "Lifecycle/CreateDefaultCameras"};
    const Quat rot = glm::quatLookAt(glm::normalize(glm::vec3(0.0f) - DEFAULT_CAMERA_POS), WORLD_UP);
    CreateCameras(state, DEFAULT_CAMERA_POS, rot, registry);
}

struct UIFontContext
{
    const IAssetLoader* assetLoader{nullptr};
    FontHandle handle{FontHandle::INVALID};
};

static UIFontContext gUI_FONT_CONTEXT{};

void LoadUIFont(EngineContext* ctx, EngineState* state)
{
    Engine::SystemRegistry registry{state->registry, Engine::AccessSet::EXCLUSIVE, nullptr, "Lifecycle/LoadUIFont"};
    const FontID robotoId = registry.Assets().FindFontByName("Roboto");
    if (robotoId.IsValid()) {
        state->uiFont = registry.Assets().LoadFont(robotoId);
    }

    gUI_FONT_CONTEXT.assetLoader = &registry.Assets();
    gUI_FONT_CONTEXT.handle = state->uiFont;

    Clay_SetMeasureTextFunction([](Clay_StringSlice text, Clay_TextElementConfig* config, void* userData) -> Clay_Dimensions {
        auto* fc = static_cast<UIFontContext*>(userData);
        const float width = MeasureText(fc->assetLoader, fc->handle, text.chars, text.length, config->fontSize, config->letterSpacing);
        const float height = config->lineHeight > 0 ? static_cast<float>(config->lineHeight) : static_cast<float>(config->fontSize);
        return {width, height};
    }, &gUI_FONT_CONTEXT);
}

void LoadStartupScene(EngineContext* ctx, EngineState* state)
{
    Engine::SystemRegistry registry{state->registry, Engine::AccessSet::EXCLUSIVE, nullptr, "Lifecycle/LoadStartupScene"};
    const auto& sceneCache = registry.Assets().GetSceneCache();
    StringID startupScene = state->projectConfig.defaultScene;
    bool bExplicit = false;
    const char* sceneOverride = Args::GetString(*state->launchArgs, ARG_SCENE);
    const char* playPath = Args::GetString(*state->launchArgs, ARG_PLAY);
    if (sceneOverride[0] != '\0') {
        startupScene = {};
        bExplicit = true;
        for (const auto& pair : sceneCache) {
            if (pair.value.sceneName == sceneOverride) {
                startupScene = pair.key;
                break;
            }
        }
    }
    else if (playPath[0] != '\0') {
        if (const auto header = ReadWPlayHeader(Core::Path(playPath))) {
            startupScene = StringID{header->sceneId};
            bExplicit = true;
        }
    }
    if (!startupScene.IsValid() && !bExplicit) {
        return;
    }

    if (!sceneCache.Contains(startupScene)) {
        if (bExplicit) {
            LOG_ERROR(Engine, "Startup scene '{}' ({:x}) not found in the scene cache", sceneOverride, startupScene.id);
        }
        return;
    }
    const auto res = LoadSceneFromFile(ctx, state, registry, startupScene);
    if (res.bSuccess) {
        state->scene.currentSceneId = res.sceneId;
        state->scene.currentSceneName = res.sceneName;
    }
}

void HotReloadSave(EngineContext* ctx, EngineState* state)
{
    Engine::SystemRegistry registry{state->registry, Engine::AccessSet::EXCLUSIVE, nullptr, "Lifecycle/HotReloadSave"};
    if (IsPlaying(state)) {
        PlayStop(ctx, state, registry);
    }
    SimulateStop(ctx, state, registry);

    {
        auto camView = registry.View<Component::EditorCameraTag, Component::TransformComponent>();
        const auto camEntity = camView.front();
        if (camEntity != entt::null) {
            const auto& transform = registry.Get<Component::TransformComponent>(camEntity);
            state->editor.pieCameraTranslation = transform.translation;
            state->editor.pieCameraRotation = transform.rotation;
        }
    }

    state->editor.hotReloadSnapshot = SerializeAll(ctx, state, registry, state->editor.loadedScenes);
    Core::InlineVector<StringID, 8> scenesToUnload;
    for (RuntimeSceneMetadata scene : state->editor.loadedScenes) {
        scenesToUnload.PushBack(scene.sceneId);
    }
    UnloadScenes(ctx, state, registry, scenesToUnload);
    LOG_INFO(Engine, "Hot reload: snapshot saved ({} scene(s))", state->editor.hotReloadSnapshot.Size());

    registry.ClearAll();
    PlaybackCommands(ctx, state, registry);
}

void HotReloadRestore(EngineContext* ctx, EngineState* state)
{
    Engine::SystemRegistry registry{state->registry, Engine::AccessSet::EXCLUSIVE, nullptr, "Lifecycle/HotReloadRestore"};
    state->editor.undo.Clear();
    CreateCameras(state, state->editor.pieCameraTranslation, state->editor.pieCameraRotation, registry);

    if (!state->editor.hotReloadSnapshot.IsEmpty()) {
        DeserializeAll(ctx, state, registry, state->editor.hotReloadSnapshot);
        state->editor.hotReloadSnapshot.Clear();
        LOG_INFO(Engine, "Hot reload: snapshot restored");
    }
}
} // Engine
