//
// Created by William on 2025-12-14.
//

#include <tracy/Tracy.hpp>

#include "spdlog/spdlog.h"

#include "engine/include/game_interface.h"
#include "../render/interface/render_interface.h"
#include "core/input/input_frame.h"
#include "engine/engine_api.h"
#include "physics/physics_system.h"

#include "imgui.h"
#include "audio/audio_manager.h"
#include "systems/render_systems.h"
#include "core/math/constants.h"

#include "fwd_components.h"
#include "component-registry/component_registry.h"
#include "components/common_components.h"
#include "engine/logging/engine_log.h"
#include "engine/logging/engine_logger.h"
#include "render/vulkan/vk_context.h"
#include "systems/debug_system.h"
#include "systems/camera_system.h"
#include "systems/editor_systems.h"
#include "systems/physics_system.h"
#include "gameplay/player/physics_player_controller.h"
#include "systems/common_systems.h"
#include "systems/core_systems.h"
#include "systems/gameplay_systems.h"
#include "engine/asset_manager.h"
#include "systems/scene_system.h"
#include "clay/clay.h"


extern "C"
{
GAME_API void GameStartup(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_TRACE("Game Start Up");

    const entt::entity editorCamera = state->registry.create();
    state->registry.emplace<Game::Component::FreeCameraComponent>(editorCamera);
    state->registry.emplace<Game::Component::CameraComponent>(editorCamera);
    state->registry.emplace<Game::Component::EditorCameraTag>(editorCamera);
    Game::Component::TransformComponent& editorCameraTransform = state->registry.emplace<Game::Component::TransformComponent>(editorCamera);
    editorCameraTransform.translation = glm::vec3(0.0f, 3.0f, 5.0f);
    editorCameraTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - glm::vec3(0.0f, 3.0f, 5.0f)), WORLD_UP);

    const entt::entity gameCamera = state->registry.create();
    state->registry.emplace<Game::Component::FreeCameraComponent>(gameCamera);
    state->registry.emplace<Game::Component::CameraComponent>(gameCamera);
    state->registry.emplace<Game::Component::GameCameraTag>(gameCamera);
    Game::Component::TransformComponent& gameCameraTransform = state->registry.emplace<Game::Component::TransformComponent>(gameCamera);
    gameCameraTransform.translation = glm::vec3(0.0f, 3.0f, 5.0f);
    gameCameraTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - glm::vec3(0.0f, 3.0f, 5.0f)), WORLD_UP);


    state->registry.ctx().emplace<Engine::EngineState*>(state);
    state->registry.ctx().emplace<Engine::EngineContext*>(ctx);
}

GAME_API void GameLoad(Engine::EngineContext* ctx, Engine::EngineState* state)
{
#ifndef GAME_STATIC
    ctx->engineLogger->RegisterLoggersForDLL(Engine::LogCategory::Game);

    ImGui::SetCurrentContext(ctx->imguiContext);
    ImGui::SetAllocatorFunctions(ctx->imguiAllocFn, ctx->imguiFreeFn, ctx->imguiAllocUserData);
    Clay_SetCurrentContext(ctx->clayContext);

    ctx->physicsSystem->RegisterPhysics();
    ctx->scheduler->RegisterExternalTaskThread();
#endif

    const Engine::FontID robotoId = ctx->assetManager->FindFontByName("Roboto");
    if (robotoId.IsValid()) {
        state->uiFont = ctx->assetManager->LoadFont(robotoId);
    }

    struct UIFontContext
    {
        Engine::AssetManager* assetManager;
        Engine::FontHandle handle;
    };
    static UIFontContext uiFontCtx{};
    uiFontCtx.assetManager = ctx->assetManager;
    uiFontCtx.handle = state->uiFont;

    Clay_SetMeasureTextFunction([](Clay_StringSlice text, Clay_TextElementConfig* config, void* userData) -> Clay_Dimensions {
        auto* fc = static_cast<UIFontContext*>(userData);
        const Engine::Font* font = fc->assetManager->GetFont(fc->handle);
        if (!font) { return {0.0f, static_cast<float>(config->fontSize)}; }
        const float scale = static_cast<float>(config->fontSize) / font->header.emSize;
        float width = 0.0f;
        for (int32_t i = 0; i < text.length; ++i) {
            const uint32_t cp = static_cast<unsigned char>(text.chars[i]);
            const Engine::WGlyphInfo* g = fc->assetManager->GetGlyph(fc->handle, cp);
            if (!g) {
                width += config->fontSize * 0.25f;
                continue;
            }
            width += g->advance * scale;
            if (i < text.length - 1) { width += config->letterSpacing; }
        }
        const float height = config->lineHeight > 0 ? static_cast<float>(config->lineHeight) : static_cast<float>(config->fontSize);
        return {width, height};
    }, &uiFontCtx);

    Audio::AudioManager::RegisterAudio();
    Game::RegisterComponents(state->componentRegistry);
    Game::ConnectPhysicsObservers(state->registry);
    Game::ConnectCommonObservers(state->registry);
    Game::ConnectRenderObservers(state->registry);

#if DEBUG
    gInternStringFn = ctx->internStringFn;
    gResolveStringIdFn = ctx->resolveStringIdFn;
#endif

#ifndef WILL_EDITOR
    if (!state->projectConfig.defaultScene.IsEmpty()) {
        const auto& sceneCache = ctx->assetManager->GetSceneCache();
        for (const auto& pair : sceneCache) {
            if (pair.value.sceneName == state->projectConfig.defaultScene.c_str()) {
                Game::LoadSceneFromFile(state, ctx->assetManager, pair.key);
                break;
            }
        }
    }
#endif
}

GAME_API void GameUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    const auto frameStart = std::chrono::high_resolution_clock::now();

#if WILL_EDITOR
    Game::EditorUpdate(ctx, state);
#endif

    Game::FunctionKeyUpdate(ctx, state);

    if (state->bIsPlaying) {
        if (state->physics.bEnabled) {
            Game::PhysicsUpdate(ctx, state);
        }
        Game::ResolveCollisionEvents(ctx, state);

        Game::DebugProcessPhysicsCollisions(ctx, state);
        Game::DebugApplyGroundForces(ctx, state);

        Game::UpdatePathMovers(ctx, state);
        Game::CheckpointUpdate(ctx, state);
        Game::DeathZoneUpdate(ctx, state);

        if (auto* playerController = state->registry.ctx().find<Game::PhysicsPlayerController>()) {
            playerController->Update(ctx, state);
        }
    }
    else {
#if WILL_EDITOR
        Game::UpdateEditorCamera(ctx, state);
#else
        Game::PlayStart(ctx, state);
#endif
    }

    Game::DebugUpdate(ctx, state);

    // Resolve Creations
#if WILL_EDITOR
    Game::ResolveModelHotReloads(ctx, state);
    Game::ResolveFontHotReloads(ctx, state);
    Game::ResolveTextureHotReloads(ctx, state);
#endif
    Game::ResolveTextLoads(ctx, state);

    if (ctx->bModelLoadedThisFrame || state->bPendingModelResolve) {
        Game::ResolveStaticMeshLoads(ctx, state);
        Game::ResolveProceduralMeshLoads(ctx, state);
        Game::ResolveSplineMeshLoads(ctx, state);

        Game::ResolvePhysicsMeshLoads(ctx, state);
        state->bPendingModelResolve = false;
    }
    Game::ResolvePhysicsShapeCreation(ctx, state);
    Game::ResolvePhysicsBodyCreation(ctx, state);

    // Dirty carry-over to next frame
    Game::MarkRenderTransformsDirty(ctx, state);
    Game::MarkPhysicsTransformsDirty(state);

    // Frame Cleanup
    state->registry.clear<Game::Component::DirtyTransformTag>();
    ctx->physicsSystem->ClearCollisionEvents();
    ctx->physicsSystem->ClearActivationEvents();
    ctx->materialManager->ProcessRetirements();

    const auto frameEnd = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart);
    constexpr auto targetFrameTime = std::chrono::microseconds(1000);

    if (elapsed < targetFrameTime) {
        ZoneScopedN("WaitForTargetFrameTime");
        std::this_thread::sleep_for(targetFrameTime - elapsed);
    }
}

static void GatherUIRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    Clay_SetLayoutDimensions({static_cast<float>(ctx->windowContext.viewportWidth), static_cast<float>(ctx->windowContext.viewportHeight)});

    const Vec2 mousePos = state->inputFrame->mousePositionAbsolute;
    const bool bIsMouseDown = state->inputFrame->GetMouse(MouseButton::LMB).down;
    const float viewportOffsetX = static_cast<float>(ctx->windowContext.viewportOffsetX);
    const float viewportOffsetY = static_cast<float>(ctx->windowContext.viewportOffsetY);
    Clay_SetPointerState(Clay_Vector2{mousePos.x - viewportOffsetX, mousePos.y - viewportOffsetY}, bIsMouseDown);

    const Vec2 mouseWheelDelta = state->inputFrame->mouseWheelDelta;
    Clay_UpdateScrollContainers(true, Clay_Vector2{mouseWheelDelta.x, mouseWheelDelta.y}, state->timeFrame->deltaTime);

    Clay_BeginLayout();

    constexpr Clay_Color COLOR_LIGHT = Clay_Color{224, 215, 210, 255};
    constexpr Clay_Color COLOR_RED = Clay_Color{168, 66, 28, 255};
    constexpr Clay_Color COLOR_ORANGE = Clay_Color{225, 138, 50, 255};

    uint32_t smilingFriendImageIndex = SMILING_FRIENDS_BINDLESS_INDEX;

    constexpr Clay_Color COLOR_DARK = Clay_Color{30, 30, 40, 240};
    constexpr Clay_Color COLOR_ITEM = Clay_Color{60, 80, 120, 255};
    constexpr Clay_Color COLOR_SCROLLBAR = Clay_Color{180, 180, 200, 160};
    constexpr Clay_Color COLOR_OVERLAY = Clay_Color{255, 120, 60, 80};

    CLAY(CLAY_ID("OuterContainer"), { .layout = { .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16 }, .backgroundColor = {250, 250, 255, 64} }) {
        CLAY(CLAY_ID("SideBar"), {
             .layout = {.sizing = {.width = CLAY_SIZING_FIXED(300), .height = CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16, .layoutDirection = CLAY_TOP_TO_BOTTOM, },
             .backgroundColor = COLOR_LIGHT
             }) {
            CLAY(CLAY_ID("ProfilePictureOuter"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 16, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = COLOR_RED }) {
                CLAY(CLAY_ID("ProfilePicture"), { .layout = { .sizing = { .width = CLAY_SIZING_FIXED(60), .height = CLAY_SIZING_FIXED(60) }}, .image = { .imageData = &smilingFriendImageIndex } }) {}
                CLAY_TEXT(CLAY_STRING("Clay - UI Library"), { .textColor = {255, 255, 255, 255}, .fontSize = 24, });
            }
            CLAY_TEXT(CLAY_STRING("WillEngine"), {.textColor = {255, 255, 255, 255}, .fontSize = 24, });

            CLAY(CLAY_ID("MainContent"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) } }, .backgroundColor = COLOR_LIGHT }) {}
        }

        // Scrollable list with overlay color tint and a floating scrollbar
        CLAY(CLAY_ID("ScrollDemo"), {
             .layout = { .sizing = { .width = CLAY_SIZING_FIXED(260), .height = CLAY_SIZING_FIXED(300) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
             .backgroundColor = COLOR_DARK,
             .overlayColor = COLOR_OVERLAY,
             }) {
            // Clipped scroll area (generates SCISSOR_START/END)
            CLAY(CLAY_ID("ScrollList"), {
                 .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(8), .childGap = 6, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                 .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() },
                 }) {
                for (int32_t i = 0; i < 32; ++i) {
                    CLAY(CLAY_IDI("ScrollItem", i), {
                         .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(36) }, .padding = { 8, 8, 6, 6 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                         .backgroundColor = COLOR_ITEM,
                         .cornerRadius = CLAY_CORNER_RADIUS(4),
                         }) {
                        CLAY_TEXT(CLAY_STRING("Item"), { .textColor = {220, 220, 255, 255}, .fontSize = 18 });
                    }
                }
            }

            // Floating scrollbar thumb
            Clay_ScrollContainerData scrollData = Clay_GetScrollContainerData(Clay_GetElementId(CLAY_STRING("ScrollList")));
            if (scrollData.found && scrollData.contentDimensions.height > scrollData.scrollContainerDimensions.height) {
                const float trackH = scrollData.scrollContainerDimensions.height;
                const float thumbH = (trackH / scrollData.contentDimensions.height) * trackH;
                const float thumbY = (-scrollData.scrollPosition->y / scrollData.contentDimensions.height) * trackH;
                CLAY(CLAY_ID("ScrollThumb"), {
                     .layout = { .sizing = { CLAY_SIZING_FIXED(6), CLAY_SIZING_FIXED(thumbH) } },
                     .backgroundColor = COLOR_SCROLLBAR,
                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                     .floating = {
                     .offset = { .x = -6, .y = thumbY },
                     .parentId = Clay_GetElementId(CLAY_STRING("ScrollList")).id,
                     .zIndex = 1,
                     .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP },
                     .attachTo = CLAY_ATTACH_TO_PARENT,
                     },
                     }) {}
            }
        }
    }

    Clay_RenderCommandArray renderCommands = Clay_EndLayout(frameBuffer->timeFrame.deltaTime);

    const float vpWidth = static_cast<float>(ctx->windowContext.viewportWidth);
    const float vpHeight = static_cast<float>(ctx->windowContext.viewportHeight);

    Core::ViewFamily& vf = frameBuffer->mainViewFamily;
    const Engine::Font* uiFont = ctx->assetManager->GetFont(state->uiFont);
    assert(uiFont);

    for (int32_t i = 0; i < renderCommands.length; ++i) {
        const Clay_RenderCommand& cmd = renderCommands.internalArray[i];

        switch (cmd.commandType) {
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
            {
                const Clay_BoundingBox& bb = cmd.boundingBox;
                Core::UIDrawCommand dc{.type = Core::UICommandType::ScissorPush};
                dc.scissor = Core::UIScissorCommand{static_cast<int32_t>(bb.x), static_cast<int32_t>(bb.y), static_cast<uint32_t>(bb.width), static_cast<uint32_t>(bb.height)};
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            {
                vf.uiDrawList.PushBack(Core::UIDrawCommand{.type = Core::UICommandType::ScissorPop});
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_START:
            {
                const Clay_Color& c = cmd.renderData.overlayColor.color;
                Core::UIDrawCommand dc{.type = Core::UICommandType::OverlayPush};
                dc.overlay = Core::UIOverlayColorCommand{.color = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f}};
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_END:
            {
                vf.uiDrawList.PushBack(Core::UIDrawCommand{.type = Core::UICommandType::OverlayPop});
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
            {
                const Clay_BoundingBox& bb = cmd.boundingBox;
                const Clay_Color& c = cmd.renderData.rectangle.backgroundColor;
                const float xMin = bb.x / vpWidth * 2.0f - 1.0f;
                const float yMin = bb.y / vpHeight * 2.0f - 1.0f;
                const float xMax = (bb.x + bb.width) / vpWidth * 2.0f - 1.0f;
                const float yMax = (bb.y + bb.height) / vpHeight * 2.0f - 1.0f;
                Core::UIDrawCommand dc{.type = Core::UICommandType::Rect};
                dc.rect = Core::UIRectDrawCall{
                    .color = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f},
                    .posMin = {xMin, yMin},
                    .posMax = {xMax, yMax},
                    .zIndex = cmd.zIndex,
                };
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE:
            {
                const Clay_BoundingBox& bb = cmd.boundingBox;
                const Clay_ImageRenderData& img = cmd.renderData.image;
                const uint32_t bindlessIndex = *static_cast<const uint32_t*>(img.imageData);

                const float xMin = bb.x / vpWidth * 2.0f - 1.0f;
                const float yMin = bb.y / vpHeight * 2.0f - 1.0f;
                const float xMax = (bb.x + bb.width) / vpWidth * 2.0f - 1.0f;
                const float yMax = (bb.y + bb.height) / vpHeight * 2.0f - 1.0f;

                const Clay_Color& tc = img.backgroundColor;
                const bool bUntinted = tc.r == 0 && tc.g == 0 && tc.b == 0 && tc.a == 0;
                const Vec4 tint = bUntinted
                                      ? Vec4{1.0f, 1.0f, 1.0f, 1.0f}
                                      : Vec4{tc.r / 255.0f, tc.g / 255.0f, tc.b / 255.0f, tc.a / 255.0f};

                Core::UIDrawCommand dc{.type = Core::UICommandType::Image};
                dc.image = Core::UIRenderCommandImage{
                    .posMin = {xMin, yMin},
                    .posMax = {xMax, yMax},
                    .uvMin = {0.0f, 1.0f}, // y flip: viewport Y-flip in SetupUIRender inverts V
                    .uvMax = {1.0f, 0.0f},
                    .tintColor = tint,
                    .imageBindlessIndex = bindlessIndex,
                    .zIndex = cmd.zIndex,
                };
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT:
            {
                const Clay_BoundingBox& bb = cmd.boundingBox;
                const Clay_TextRenderData& td = cmd.renderData.text;
                const float fontSize = td.fontSize;
                const float scale = fontSize / uiFont->header.emSize;
                const Vec4 color{td.textColor.r / 255.0f, td.textColor.g / 255.0f, td.textColor.b / 255.0f, td.textColor.a / 255.0f};

                const auto quadStart = static_cast<uint32_t>(vf.uiGlyphQuads.Size());
                uint32_t quadCount = 0;

                float cursorX = bb.x;
                const float baselineY = bb.y + fontSize;

                for (int32_t ci = 0; ci < td.stringContents.length; ++ci) {
                    const uint32_t cp = static_cast<unsigned char>(td.stringContents.chars[ci]);
                    const Engine::WGlyphInfo* g = ctx->assetManager->GetGlyph(state->uiFont, cp);
                    if (!g) {
                        cursorX += fontSize * 0.25f;
                        continue;
                    }

                    const float xMin = (cursorX + g->planeLeft * scale) / vpWidth * 2.0f - 1.0f;
                    const float yMax = (baselineY - g->planeBottom * scale) / vpHeight * 2.0f - 1.0f;
                    const float xMax = (cursorX + g->planeRight * scale) / vpWidth * 2.0f - 1.0f;
                    const float yMin = (baselineY - g->planeTop * scale) / vpHeight * 2.0f - 1.0f;

                    vf.uiGlyphQuads.PushBack(UIGlyphQuad{
                        .color = {color.x, color.y, color.z, color.w},
                        .posMin = {xMin, yMin},
                        .posMax = {xMax, yMax},
                        .uvMin = {g->uvLeft, g->uvBottom},
                        .uvMax = {g->uvRight, g->uvTop},
                        .uvOrigMin = {g->uvLeft, g->uvBottom},
                        .uvOrigMax = {g->uvRight, g->uvTop},
                    });
                    ++quadCount;

                    cursorX += g->advance * scale + td.letterSpacing;
                }

                if (quadCount == 0) { break; }

                Core::UIDrawCommand dc{.type = Core::UICommandType::Text};
                dc.text = Core::UITextDrawCall{
                    .quadOffset = quadStart,
                    .quadCount = quadCount,
                    .atlasBindlessIndex = uiFont->atlasTexture.bindlessHandle.index,
                    .pxRange = static_cast<float>(uiFont->header.sdfSpread),
                    .zIndex = cmd.zIndex,
                };
                vf.uiDrawList.PushBack(dc);
                break;
            }
            default: break;
        }
    }
}

GAME_API void GamePrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    Game::FunctionKeyRenderUpdate(ctx, state, frameBuffer);

    Game::BuildViewFamily(state, frameBuffer->mainViewFamily);
    frameBuffer->bWireframe = state->debug.bWireframe;
    frameBuffer->bEnableShadeDispatchBucketingVisualization = state->debug.bEnableShadeDispatchBucketingVisualization;
    frameBuffer->bEnableLightingBucketingVisualization = state->debug.bEnableLightingBucketingVisualization;
    if (state->debug.bEnablePortal) {
        Game::BuildPortalViewFamily(state, frameBuffer->mainViewFamily);
    }

    Game::RenderPrepareTransforms(ctx, state, frameBuffer);
    Game::GatherRenderables(ctx, state, frameBuffer);
    Game::GatherTextRenderables(ctx, state, frameBuffer);
    GatherUIRenderables(ctx, state, frameBuffer);

#if WILL_EDITOR
    Game::DrawEditorInterface(ctx, state, frameBuffer);
#endif

#ifndef PACKAGED_BUILD
    Game::DebugRender(ctx, state, frameBuffer);
    Game::DebugRenderPhysics(ctx, state, frameBuffer);
#endif
}

GAME_API void GameEndFrame(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ctx->gameplayArena.Get().Reset();
#if WILL_EDITOR
    ctx->editorArena.Get().Reset();
#endif
}

GAME_API void GameUnload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
#ifndef GAME_STATIC
    if (ctx->scheduler) {
        ctx->scheduler->DeRegisterExternalTaskThread();
    }
#endif
}

GAME_API void GameShutdown(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_TRACE("Game Shutdown");
}
}
