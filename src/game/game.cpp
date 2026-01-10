//
// Created by William on 2025-12-14.
//

#include "spdlog/spdlog.h"

#include "core/include/game_interface.h"
#include "core/include/render_interface.h"
#include "core/input/input_frame.h"
#include "engine/engine_api.h"
#include "physics/physics_system.h"
#include "fwd_components.h"
#include "imgui.h"
#include "components/render/gather_renderables_component.h"
#include "core/math/constants.h"
#include "systems/debug_system.h"
#include "systems/camera_system.h"
#include "systems/physics_system.h"


extern "C"
{
GAME_API void GameStartup(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Start Up");

    const entt::entity camera = state->registry.create();
    state->registry.emplace<Game::FreeCameraComponent>(camera);
    state->registry.emplace<Game::CameraComponent>(camera);
    Game::TransformComponent& cameraTransform = state->registry.emplace<Game::TransformComponent>(camera);
    cameraTransform.translation = glm::vec3(0.0f, 3.0f, 5.0f);
    cameraTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - glm::vec3(0.0f, 3.0f, 5.0f)), WORLD_UP);
    state->registry.emplace<Game::MainViewportComponent>(camera);
    state->registry.emplace<Game::RenderDebugViewComponent>(camera);
    state->registry.ctx().emplace<Engine::GameState*>(state);

    spdlog::set_default_logger(ctx->logger);
}

GAME_API void GameLoad(Core::EngineContext* ctx, Engine::GameState* state)
{
    spdlog::set_default_logger(ctx->logger);
    ImGui::SetCurrentContext(ctx->imguiContext);

    ctx->physicsSystem->RegisterAllocator();
    state->registry.on_construct<Game::PhysicsBodyComponent>().connect<&Game::OnPhysicsBodyAdded>();
    state->registry.on_destroy<Game::PhysicsBodyComponent>().connect<&Game::OnPhysicsBodyRemoved>();
}

GAME_API void GameUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    Game::System::UpdateCameras(ctx, state);
    Game::System::DebugUpdate(ctx, state);

    Game::System::DebugProcessPhysicsCollisions(ctx, state);
    Game::System::DebugApplyGroundForces(ctx, state);

    if (state->bEnablePhysics) {
        Game::System::UpdatePhysics(ctx, state);
    }

    Core::InputFrame gameInputCopy = *state->inputFrame;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

GAME_API void GamePrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    Game::System::BuildViewFamily(state, frameBuffer->mainViewFamily);
    Game::System::GatherRenderables(ctx, state, frameBuffer);

    if (ImGui::Begin("Test 2")) {
        ImGui::Checkbox("Enable Physics", &state->bEnablePhysics);

        if (ImGui::CollapsingHeader("Shadow Settings")) {
            const char* qualityNames[] = {"Ultra", "High", "Medium", "Low", "Custom"};
            int currentQuality = static_cast<int>(state->shadowQuality);
            if (ImGui::Combo("Quality", &currentQuality, qualityNames, 5)) {
                state->shadowQuality = static_cast<Core::ShadowQuality>(currentQuality);
                if (currentQuality < 4) {
                    // Copy preset into cascadePreset
                    state->shadowConfig.cascadePreset = Render::SHADOW_PRESETS[currentQuality];
                }
            }

            // Display current preset data (read-only)
            ImGui::Separator();
            ImGui::Text("Current Configuration:");
            for (int i = 0; i < 4; ++i) {
                ImGui::Text("Cascade %d:", i);
                ImGui::Indent();
                ImGui::Text("  Resolution: %dx%d",
                    state->shadowConfig.cascadePreset.extents[i].width,
                    state->shadowConfig.cascadePreset.extents[i].height);
                ImGui::Text("  Bias: %.2f/%.2f",
                    state->shadowConfig.cascadePreset.biases[i].linear,
                    state->shadowConfig.cascadePreset.biases[i].sloped);
                ImGui::Text("  PCSS Samples: %u blocker, %u PCF",
                    state->shadowConfig.cascadePreset.pcssSamples[i].blockerSearchSamples,
                    state->shadowConfig.cascadePreset.pcssSamples[i].pcfSamples);
                ImGui::Text("  Light Size: %.4f",
                    state->shadowConfig.cascadePreset.lightSizes[i]);
                ImGui::Unindent();
            }

            // Custom quality editing
            if (state->shadowQuality == Core::ShadowQuality::Custom) {
                ImGui::Separator();
                ImGui::Text("Custom Settings:");

                static Render::ShadowCascadePreset customPreset = state->shadowConfig.cascadePreset;

                for (int i = 0; i < 4; ++i) {
                    ImGui::PushID(i);
                    if (ImGui::TreeNode("Cascade", "Cascade %d", i)) {
                        ImGui::InputInt("Width", reinterpret_cast<int*>(&customPreset.extents[i].width));
                        ImGui::InputInt("Height", reinterpret_cast<int*>(&customPreset.extents[i].height));
                        ImGui::InputFloat("Linear Bias", &customPreset.biases[i].linear);
                        ImGui::InputFloat("Sloped Bias", &customPreset.biases[i].sloped);
                        ImGui::InputScalar("Blocker Samples", ImGuiDataType_U32, &customPreset.pcssSamples[i].blockerSearchSamples);
                        ImGui::InputScalar("PCF Samples", ImGuiDataType_U32, &customPreset.pcssSamples[i].pcfSamples);
                        ImGui::InputFloat("Light Size", &customPreset.lightSizes[i]);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }

                if (ImGui::Button("Apply Custom Settings")) {
                    state->shadowConfig.cascadePreset = customPreset;
                }
            }

            ImGui::Separator();
            ImGui::SliderFloat("Split Lambda", &state->shadowConfig.splitLambda, 0.0f, 1.0f);
            ImGui::SliderFloat("Split Overlap", &state->shadowConfig.splitOverlap, 1.0f, 1.2f);
            ImGui::Checkbox("Enabled", &state->shadowConfig.enabled);
        }
    }

    frameBuffer->mainViewFamily.shadowConfig = state->shadowConfig;

    ImGui::End();
}

GAME_API void GameUnload(Core::EngineContext* ctx, Engine::GameState* state)
{
    state->registry.on_construct<Game::PhysicsBodyComponent>().disconnect();
    state->registry.on_destroy<Game::PhysicsBodyComponent>().disconnect();
}

GAME_API void GameShutdown(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Shutdown");
}
}
