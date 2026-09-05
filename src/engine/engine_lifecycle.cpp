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
#include "engine/systems/scene_system.h"

namespace Engine
{
static constexpr Vec3 DEFAULT_CAMERA_POS{0.0f, 3.0f, 5.0f};

void CreateCameras(EngineState* state, Vec3 editorPos, Quat editorRot)
{
    const entt::entity editorCamera = state->registry.create();
    state->registry.emplace<Component::FreeCameraComponent>(editorCamera);
    state->registry.emplace<Component::CameraComponent>(editorCamera);
    state->registry.emplace<Component::EditorCameraTag>(editorCamera);
    auto& editorCameraTransform = state->registry.emplace<Component::TransformComponent>(editorCamera);
    editorCameraTransform.translation = editorPos;
    editorCameraTransform.rotation = editorRot;

    const entt::entity gameCamera = state->registry.create();
    state->registry.emplace<Component::FreeCameraComponent>(gameCamera);
    state->registry.emplace<Component::CameraComponent>(gameCamera);
    state->registry.emplace<Component::GameCameraTag>(gameCamera);
    auto& gameCameraTransform = state->registry.emplace<Component::TransformComponent>(gameCamera);
    gameCameraTransform.translation = DEFAULT_CAMERA_POS;
    gameCameraTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.0f) - DEFAULT_CAMERA_POS), WORLD_UP);
}

void CreateDefaultCameras(EngineState* state)
{
    const Quat rot = glm::quatLookAt(glm::normalize(glm::vec3(0.0f) - DEFAULT_CAMERA_POS), WORLD_UP);
    CreateCameras(state, DEFAULT_CAMERA_POS, rot);
}

struct UIFontContext
{
    AssetManager* assetManager{nullptr};
    FontHandle handle{FontHandle::INVALID};
};

static UIFontContext gUI_FONT_CONTEXT{};

void LoadUIFont(EngineContext* ctx, EngineState* state)
{
    const FontID robotoId = ctx->assetManager->FindFontByName("Roboto");
    if (robotoId.IsValid()) {
        state->uiFont = ctx->assetManager->LoadFont(robotoId);
    }

    gUI_FONT_CONTEXT.assetManager = ctx->assetManager;
    gUI_FONT_CONTEXT.handle = state->uiFont;

    Clay_SetMeasureTextFunction([](Clay_StringSlice text, Clay_TextElementConfig* config, void* userData) -> Clay_Dimensions {
        auto* fc = static_cast<UIFontContext*>(userData);
        const float width = MeasureText(fc->assetManager, fc->handle, text.chars, text.length, config->fontSize, config->letterSpacing);
        const float height = config->lineHeight > 0 ? static_cast<float>(config->lineHeight) : static_cast<float>(config->fontSize);
        return {width, height};
    }, &gUI_FONT_CONTEXT);
}

void LoadStartupScene(EngineContext* ctx, EngineState* state)
{
    const char* startupScene = !state->automation.sceneOverride.IsEmpty() ? state->automation.sceneOverride.c_str() : state->projectConfig.defaultScene.c_str();
    if (startupScene[0] == '\0') {
        return;
    }

    bool bFound = false;
    const auto& sceneCache = ctx->assetManager->GetSceneCache();
    for (const auto& pair : sceneCache) {
        if (pair.value.sceneName == startupScene) {
            auto res = LoadSceneFromFile(state, ctx->assetManager, pair.key);
            if (res.bSuccess) {
                state->scene.currentSceneId = res.sceneId;
                state->scene.currentSceneName = res.sceneName;
            }
            bFound = true;
            break;
        }
    }
    if (!bFound && !state->automation.sceneOverride.IsEmpty()) {
        LOG_ERROR(Engine, "--scene '{}' not found in the scene cache", startupScene);
    }
}

void HotReloadSave(EngineContext* ctx, EngineState* state)
{
    if (IsPlaying(state)) {
        PlayStop(ctx, state);
    }

    {
        auto camView = state->registry.view<Component::EditorCameraTag, Component::TransformComponent>();
        const auto camEntity = camView.front();
        if (camEntity != entt::null) {
            const auto& transform = state->registry.get<Component::TransformComponent>(camEntity);
            state->editor.pieCameraTranslation = transform.translation;
            state->editor.pieCameraRotation = transform.rotation;
        }
    }

    state->editor.hotReloadSnapshot = SerializeAll(state->componentRegistry, state->registry, ctx->assetManager, state->editor.loadedScenes);
    Core::InlineVector<StringID, 8> scenesToUnload;
    for (RuntimeSceneMetadata scene : state->editor.loadedScenes) {
        scenesToUnload.PushBack(scene.sceneId);
    }
    UnloadScenes(state, scenesToUnload);
    LOG_INFO(Engine, "Hot reload: snapshot saved ({} scene(s))", state->editor.hotReloadSnapshot.Size());

    state->registry.clear();
    PlaybackCommands(ctx, state);
}

void HotReloadRestore(EngineContext* ctx, EngineState* state)
{
    CreateCameras(state, state->editor.pieCameraTranslation, state->editor.pieCameraRotation);

    if (!state->editor.hotReloadSnapshot.IsEmpty()) {
        DeserializeAll(state, state->editor.hotReloadSnapshot);
        state->editor.hotReloadSnapshot.Clear();
        LOG_INFO(Engine, "Hot reload: snapshot restored");
    }
}
} // Engine
