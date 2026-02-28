//
// Created by William on 2026-01-30.
//

#include "editor_systems.h"

#include <algorithm>

#include <tracy/Tracy.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "core/include/engine_context.h"
#include "engine/engine_api.h"
#include "game/fwd_components.h"
#include "game/components/common_components.h"

namespace Game::System
{
void EditorUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    if (state->inputFrame->GetKey(Key::W).pressed) {
        state->currentGizmoOperation = ImGuizmo::TRANSLATE;
    }
    else if (state->inputFrame->GetKey(Key::E).pressed) {
        state->currentGizmoOperation = ImGuizmo::ROTATE;
    }
    else if (state->inputFrame->GetKey(Key::R).pressed) {
        state->currentGizmoOperation = ImGuizmo::SCALE;
    }
    if (!ctx->bImguiMouseCaptured && !ctx->bImguiKeyboardCaptured) {
        if (state->inputFrame->GetMouse(MouseButton::LMB).pressed) {
            const bool ctrlHeld = state->inputFrame->GetKey(Key::LCTRL).down
                                  || state->inputFrame->GetKey(Key::RCTRL).down;

            auto it = state->stableIdToEntityMap.find(StringID{ctx->lastKnownStableIdUnderCursor});
            if (it != state->stableIdToEntityMap.end()) {
                entt::entity clicked = it->second;
                if (ctrlHeld) {
                    auto pos = std::find(state->selectedEntities.begin(), state->selectedEntities.end(), clicked);
                    if (pos != state->selectedEntities.end()) {
                        state->selectedEntities.erase(pos);
                    }
                    else {
                        state->selectedEntities.push_back(clicked);
                    }
                }
                else {
                    state->selectedEntities = {clicked};
                }
            }
            else if (!ctrlHeld) {
                state->selectedEntities.clear();
            }
        }
    }
}

void DrawEditorInterface(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    if (ImGui::Begin("Debug View")) {
        auto cameraView = state->registry.view<Component::CameraComponent, Component::MainViewportComponent, Component::TransformComponent>();
        const auto& [cam, transform] = cameraView.get(cameraView.front());
        ImGui::Text("Camera Pos: (%.2f, %.2f, %.2f)",
                    transform.translation.x, transform.translation.y, transform.translation.z);
        ImGui::Text("Camera Forward: (%.2f, %.2f, %.2f)",
                    cam.currentViewData.cameraForward.x,
                    cam.currentViewData.cameraForward.y,
                    cam.currentViewData.cameraForward.z);

        ImGui::Text("Current Debug View: %s", state->debugResourceName.empty() ? "None" : state->debugResourceName.c_str());
        ImGui::Checkbox("Enable Portals", &state->bEnablePortal);

        if (ImGui::Button("Disable Debug View")) {
            state->debugResourceName.clear();
        }

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Hotkeys", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* keyNames[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
            for (size_t i = 0; i < std::size(DEBUG_HOTKEYS); ++i) {
                ImGui::Text("%s: %s (%s)", keyNames[i], DEBUG_HOTKEYS[i].name, DEBUG_HOTKEYS[i].resourceName);
            }
        }

        ImGui::Separator();

        auto setDebugTarget = [&](const char* name, DebugTransformationType _transform, Core::DebugViewAspect aspect) {
            if (state->debugResourceName == name && state->debugViewAspect == aspect) {
                state->debugResourceName.clear();
            }
            else {
                state->debugResourceName = name;
                state->debugTransformationType = _transform;
                state->debugViewAspect = aspect;
            }
        };
        if (ImGui::CollapsingHeader("G-Buffer")) {
            if (ImGui::Button("Depth Target")) setDebugTarget("depth_target", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Stencil Target")) setDebugTarget("depth_target", DebugTransformationType::StencilRemap, Core::DebugViewAspect::Stencil);
            if (ImGui::Button("Albedo Target")) setDebugTarget("albedo_target", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Normal Target")) setDebugTarget("normal_target", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("PBR Target")) setDebugTarget("pbr_target", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Emissive Target")) setDebugTarget("emissive_target", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Velocity Target")) setDebugTarget("velocity_target", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Shadows")) {
            if (ImGui::Button("Shadow Cascade 0")) setDebugTarget("shadow_cascade_0", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Shadow Cascade 1")) setDebugTarget("shadow_cascade_1", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Shadow Cascade 2")) setDebugTarget("shadow_cascade_2", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Shadow Cascade 3")) setDebugTarget("shadow_cascade_3", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Shadows Resolve")) setDebugTarget("shadows_resolve_target", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Lighting")) {
            if (ImGui::Button("Deferred Resolve")) setDebugTarget("deferred_resolve_target", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Depth")) setDebugTarget("gtao_depth", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO AO")) setDebugTarget("gtao_ao", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Edges")) setDebugTarget("gtao_edges", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Filtered")) setDebugTarget("gtao_filtered", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Anti-Aliasing")) {
            if (ImGui::Button("TAA Current")) setDebugTarget("taa_current", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("TAA Output")) setDebugTarget("taa_output", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Portal")) {
            if (ImGui::Button("Portal Albedo")) setDebugTarget("portal_albedo", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Portal Normal")) setDebugTarget("portal_normal", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Portal PBR")) setDebugTarget("portal_pbr", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Portal Emissive")) setDebugTarget("portal_emissive", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Portal Velocity")) setDebugTarget("portal_velocity", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Portal Depth")) setDebugTarget("portal_depth", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Portal Deferred Resolve")) setDebugTarget("portal_deferred_resolve", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Post-Processing")) {
            if (ImGui::Button("Bloom Chain")) setDebugTarget("bloom_chain", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Sharpening Output")) setDebugTarget("sharpening_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Tonemap Output")) setDebugTarget("tonemap_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Motion Blur Tiled Max")) setDebugTarget("motion_blur_tiled_max", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Motion Blur Neighbor Max")) setDebugTarget("motion_blur_tiled_neighbor_max", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Motion Blur Output")) setDebugTarget("motion_blur_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Color Grading Output")) setDebugTarget("color_grading_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Vignette Aberration Output")) setDebugTarget("vignette_aberration_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Post Process Output")) setDebugTarget("post_process_output", DebugTransformationType::None, Core::DebugViewAspect::None);
        }
    }
    ImGui::End();


    ImGuizmo::SetOrthographic(false);
    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(
        static_cast<float>(ctx->windowContext.viewportOffsetX),
        static_cast<float>(ctx->windowContext.viewportOffsetY),
        static_cast<float>(ctx->windowContext.viewportWidth),
        static_cast<float>(ctx->windowContext.viewportHeight)
    );

    if (ImGui::Begin("Details")) {
        if (ImGui::RadioButton("Translate", state->currentGizmoOperation == ImGuizmo::TRANSLATE)) state->currentGizmoOperation = ImGuizmo::TRANSLATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", state->currentGizmoOperation == ImGuizmo::ROTATE)) state->currentGizmoOperation = ImGuizmo::ROTATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", state->currentGizmoOperation == ImGuizmo::SCALE)) state->currentGizmoOperation = ImGuizmo::SCALE;

        const bool multiSelected = state->selectedEntities.size() > 1;

        // Only world space for multi-select.
        ImGui::BeginDisabled(state->currentGizmoOperation == ImGuizmo::SCALE || multiSelected);
        if (ImGui::RadioButton("Local", state->currentGizmoMode == ImGuizmo::LOCAL)) state->currentGizmoMode = ImGuizmo::LOCAL;
        ImGui::SameLine();
        if (ImGui::RadioButton("World", state->currentGizmoMode == ImGuizmo::WORLD)) state->currentGizmoMode = ImGuizmo::WORLD;
        ImGui::EndDisabled();

        if (multiSelected) {
            state->currentGizmoMode = ImGuizmo::WORLD;
        }

        ImGui::Separator();

        glm::mat4 view = frameBuffer->mainViewFamily.mainView.currentViewData.view;
        glm::mat4 proj = frameBuffer->mainViewFamily.mainView.currentViewData.proj;

        if (state->selectedEntities.size() == 1) {
            entt::entity entity = state->selectedEntities[0];
            ImGui::Text("Entity: %u", static_cast<uint32_t>(entity));

            for (Core::ComponentEntry& entry : state->componentRegistry.registry) {
                if (entry.has(state->registry, entity)) {
                    entry.drawEditor(state->registry, entity);
                }
            }
            if (auto* stableIdComponent = state->registry.try_get<Component::StableIdComponent>(entity)) {}

            if (auto* transform = state->registry.try_get<Component::TransformComponent>(entity)) {
                ImGui::Separator();

                bool dirty = false;
                dirty |= ImGui::DragFloat3("Translation", &transform->translation.x, 0.1f);
                glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(transform->rotation));
                if (ImGui::DragFloat3("Rotation", &eulerDegrees.x, 0.5f)) {
                    transform->rotation = glm::quat(glm::radians(eulerDegrees));
                    dirty = true;
                }
                dirty |= ImGui::DragFloat3("Scale", &transform->scale.x, 0.01f);

                glm::mat4 model = GetMatrix(*transform);
                ImGuizmo::Manipulate(
                    glm::value_ptr(view),
                    glm::value_ptr(proj),
                    state->currentGizmoOperation,
                    state->currentGizmoMode,
                    glm::value_ptr(model)
                );

                if (ImGuizmo::IsUsing()) {
                    float translation[3], rotation[3], scale[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), translation, rotation, scale);
                    transform->translation = glm::vec3(translation[0], translation[1], translation[2]);
                    transform->rotation = glm::quat(glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2])));
                    transform->scale = glm::vec3(scale[0], scale[1], scale[2]);
                    dirty = true;
                }

                if (dirty) {
                    state->registry.emplace_or_replace<Component::DirtyRenderTransformTag>(entity);
                    state->registry.emplace_or_replace<Component::TeleportPhysicsTransformTag>(entity);
                }
            }
        }
        else if (multiSelected) {
            ImGui::Text("%zu entities selected", state->selectedEntities.size());
            ImGui::Text("Ctrl+Click to add/remove entities");

            // Compute centroid of all selected entities that have a transform
            glm::vec3 averagePos{0.0f};
            int transformCount = 0;
            for (auto entity : state->selectedEntities) {
                if (auto* tf = state->registry.try_get<Component::TransformComponent>(entity)) {
                    averagePos += tf->translation;
                    ++transformCount;
                }
            }

            if (transformCount > 0) {
                averagePos /= static_cast<float>(transformCount);
                ImGui::Text("Centroid: (%.2f, %.2f, %.2f)", averagePos.x, averagePos.y, averagePos.z);

                static glm::quat s_prevRotation{1.0f, 0.0f, 0.0f, 0.0f};
                static glm::vec3 s_prevScale{1.0f, 1.0f, 1.0f};

                glm::mat4 gizmoMatrix = glm::translate(glm::mat4(1.0f), averagePos);
                ImGuizmo::Manipulate(
                    glm::value_ptr(view),
                    glm::value_ptr(proj),
                    state->currentGizmoOperation,
                    ImGuizmo::WORLD,
                    glm::value_ptr(gizmoMatrix)
                );

                if (ImGuizmo::IsUsing()) {
                    float t[3], r[3], s[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(gizmoMatrix), t, r, s);

                    const glm::vec3 newT = glm::vec3(t[0], t[1], t[2]);
                    const glm::quat newR = glm::quat(glm::radians(glm::vec3(r[0], r[1], r[2])));
                    const glm::vec3 newS = glm::vec3(s[0], s[1], s[2]);

                    const glm::vec3 deltaTranslation = newT - averagePos;
                    const glm::quat deltaRotation = newR * glm::conjugate(s_prevRotation);
                    const glm::vec3 deltaScale = newS / s_prevScale;

                    for (auto entity : state->selectedEntities) {
                        auto* transform = state->registry.try_get<Component::TransformComponent>(entity);
                        if (!transform) continue;

                        transform->translation += deltaTranslation;

                        glm::vec3 rel = transform->translation - averagePos;
                        transform->translation = averagePos + deltaRotation * rel;
                        transform->rotation = deltaRotation * transform->rotation;

                        rel = transform->translation - averagePos;
                        transform->translation = averagePos + rel * deltaScale;
                        transform->scale *= deltaScale;

                        state->registry.emplace_or_replace<Component::DirtyRenderTransformTag>(entity);
                        state->registry.emplace_or_replace<Component::TeleportPhysicsTransformTag>(entity);
                    }

                    s_prevRotation = newR;
                    s_prevScale = newS;
                }
                else {
                    // Reset each frame we're not dragging so the next drag starts from identity
                    s_prevRotation = {1.0f, 0.0f, 0.0f, 0.0f};
                    s_prevScale = {1.0f, 1.0f, 1.0f};
                }
            }
        }
    }
    ImGui::End();


    if (ImGui::Begin("Post-Processing")) {
        constexpr Core::PostProcessConfiguration defaultPP{};
        if (ImGui::Button("Reset All to Defaults")) {
            state->postProcess = defaultPP;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable All Effects")) {
            state->postProcess.bEnableTemporalAntialiasing = false;
            state->postProcess.tonemapOperator = -1;
            state->postProcess.bloomIntensity = 0.0f;
            state->postProcess.motionBlurVelocityScale = 0.0f;
            state->postProcess.chromaticAberrationStrength = 0.0f;
            state->postProcess.vignetteStrength = 0.0f;
            state->postProcess.grainStrength = 0.0f;
            state->postProcess.sharpeningStrength = 0.0f;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Ground Truth Ambient Occlusion");
        ImGui::Checkbox("Enable GTAO", &state->gtaoConfig.bEnabled);

        ImGui::Spacing();
        ImGui::SeparatorText("Anti-Aliasing");
        ImGui::Checkbox("Enable TAA", &state->postProcess.bEnableTemporalAntialiasing);

        ImGui::Spacing();
        ImGui::SeparatorText("Tonemapping");
        const char* tonemapOperators[] = {"None", "ACES", "Uncharted 2", "Reinhard", "Lottes"};
        int currentItem = state->postProcess.tonemapOperator + 1;
        if (ImGui::Combo("Operator", &currentItem, tonemapOperators, IM_ARRAYSIZE(tonemapOperators))) {
            state->postProcess.tonemapOperator = currentItem - 1;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Exposure");
        ImGui::SliderFloat("Target Luminance", &state->postProcess.exposureTargetLuminance, 0.01f, 1.0f, "%.3f");
        ImGui::SliderFloat("Adaptation Speed", &state->postProcess.exposureAdaptationRate, 0.1f, 50.0f, "%.1f");
        if (ImGui::Button("Reset Exposure")) {
            state->postProcess.exposureTargetLuminance = defaultPP.exposureTargetLuminance;
            state->postProcess.exposureAdaptationRate = defaultPP.exposureAdaptationRate;
        }


        ImGui::Spacing();
        ImGui::SeparatorText("Bloom");
        ImGui::SliderFloat("Intensity", &state->postProcess.bloomIntensity, 0.0f, 0.2f, "%.3f");
        ImGui::SliderFloat("Threshold", &state->postProcess.bloomThreshold, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Soft Threshold", &state->postProcess.bloomSoftThreshold, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Radius", &state->postProcess.bloomRadius, 0.5f, 2.0f, "%.2f");
        if (ImGui::Button("Reset Bloom")) {
            state->postProcess.bloomIntensity = defaultPP.bloomIntensity;
            state->postProcess.bloomThreshold = defaultPP.bloomThreshold;
            state->postProcess.bloomSoftThreshold = defaultPP.bloomSoftThreshold;
            state->postProcess.bloomRadius = defaultPP.bloomRadius;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable Bloom")) {
            state->postProcess.bloomIntensity = 0.0f;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Motion Blur");
        ImGui::DragFloat("Velocity Scale", &state->postProcess.motionBlurVelocityScale, 0.05f, 0.0f, 4.0f, "%.2f");
        ImGui::DragFloat("Depth Scale", &state->postProcess.motionBlurDepthScale, 0.1f, 2.0f, 10.0f, "%.2f");
        if (ImGui::Button("Reset Motion Blur")) {
            state->postProcess.motionBlurVelocityScale = defaultPP.motionBlurVelocityScale;
            state->postProcess.motionBlurDepthScale = defaultPP.motionBlurDepthScale;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable Motion Blur")) {
            state->postProcess.motionBlurVelocityScale = 0.0f;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Color Grading");
        ImGui::SliderFloat("Exposure Offset", &state->postProcess.colorGradingExposure, -2.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Contrast", &state->postProcess.colorGradingContrast, 0.5f, 2.0f, "%.2f");
        ImGui::SliderFloat("Saturation", &state->postProcess.colorGradingSaturation, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Temperature", &state->postProcess.colorGradingTemperature, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Tint", &state->postProcess.colorGradingTint, -1.0f, 1.0f, "%.2f");
        if (ImGui::Button("Reset Color Grading")) {
            state->postProcess.colorGradingExposure = defaultPP.colorGradingExposure;
            state->postProcess.colorGradingContrast = defaultPP.colorGradingContrast;
            state->postProcess.colorGradingSaturation = defaultPP.colorGradingSaturation;
            state->postProcess.colorGradingTemperature = defaultPP.colorGradingTemperature;
            state->postProcess.colorGradingTint = defaultPP.colorGradingTint;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Chromatic Aberration");
        ImGui::SliderFloat("Aberration Strength", &state->postProcess.chromaticAberrationStrength, 0.0f, 100.0f, "%.2f");
        if (ImGui::Button("Reset Aberration")) {
            state->postProcess.chromaticAberrationStrength = defaultPP.chromaticAberrationStrength;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable Aberration")) {
            state->postProcess.chromaticAberrationStrength = 0.0f;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Vignette");
        ImGui::SliderFloat("Vignette Strength", &state->postProcess.vignetteStrength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Vignette Radius", &state->postProcess.vignetteRadius, 0.5f, 1.0f, "%.2f");
        ImGui::SliderFloat("Vignette Smoothness", &state->postProcess.vignetteSmoothness, 0.1f, 1.0f, "%.2f");
        if (ImGui::Button("Reset Vignette")) {
            state->postProcess.vignetteStrength = defaultPP.vignetteStrength;
            state->postProcess.vignetteRadius = defaultPP.vignetteRadius;
            state->postProcess.vignetteSmoothness = defaultPP.vignetteSmoothness;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable Vignette")) {
            state->postProcess.vignetteStrength = 0.0f;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Film Grain");
        ImGui::SliderFloat("Grain Strength", &state->postProcess.grainStrength, 0.0f, 0.15f, "%.3f");
        ImGui::SliderFloat("Grain Size", &state->postProcess.grainSize, 1.0f, 3.0f, "%.2f");
        if (ImGui::Button("Reset Grain")) {
            state->postProcess.grainStrength = defaultPP.grainStrength;
            state->postProcess.grainSize = defaultPP.grainSize;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable Grain")) {
            state->postProcess.grainStrength = 0.0f;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Sharpening");
        ImGui::SliderFloat("Sharpening Strength", &state->postProcess.sharpeningStrength, 0.0f, 100.0f, "%.02f");
        if (ImGui::Button("Reset Sharpening")) {
            state->postProcess.sharpeningStrength = defaultPP.sharpeningStrength;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable Sharpening")) {
            state->postProcess.sharpeningStrength = 0.0f;
        }
    }
    ImGui::End();

    if (ImGui::Begin("Scene")) {
        ImGui::Checkbox("Enable Physics", &state->bEnablePhysics);

        if (ImGui::CollapsingHeader("Directional Light")) {
            ImGui::SliderFloat3("Direction", &state->directionalLight.direction.x, -1.0f, 1.0f);
            if (ImGui::Button("Normalize Direction")) {
                frameBuffer->mainViewFamily.directionalLight.direction = glm::normalize(state->directionalLight.direction);
            }
            ImGui::SliderFloat("Intensity", &state->directionalLight.intensity, 0.0f, 5.0f);
            ImGui::ColorEdit3("Color", &state->directionalLight.color.x);
        }

        if (ImGui::CollapsingHeader("Shadow Settings")) {
            const char* qualityNames[] = {"Ultra", "High", "Medium", "Low", "Custom"};
            int currentQuality = static_cast<int>(state->shadowQuality);
            if (ImGui::Combo("Quality", &currentQuality, qualityNames, 5)) {
                state->shadowQuality = static_cast<Core::ShadowQuality>(currentQuality);
                if (currentQuality < 4) {
                    state->shadowConfig.cascadePreset = Render::SHADOW_PRESETS[currentQuality];
                }
            }

            ImGui::SliderFloat("Shadow Intensity", &state->shadowConfig.shadowIntensity, 0.0f, 1.0f);

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
    ImGui::End();

    frameBuffer->mainViewFamily.directionalLight = state->directionalLight;
    frameBuffer->mainViewFamily.shadowConfig = state->shadowConfig;
    frameBuffer->mainViewFamily.postProcessConfig = state->postProcess;
    frameBuffer->mainViewFamily.gtaoConfig = state->gtaoConfig;
    frameBuffer->mainViewFamily.debugResourceName = state->debugResourceName;
    frameBuffer->mainViewFamily.debugTransformationType = state->debugTransformationType;
    frameBuffer->mainViewFamily.debugViewAspect = state->debugViewAspect;
}

template<>
void DrawComponentEditor<Component::TransformComponent>(Component::TransformComponent& component, entt::registry& registry, entt::entity entity)
{}

template<>
void DrawComponentEditor<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, entt::registry& registry, entt::entity entity) {}

template<>
void DrawComponentEditor<Component::StableIdComponent>(Component::StableIdComponent& component, entt::registry& registry, entt::entity entity)
{
    ImGui::Separator();
    ImGui::Text("StableID: %llu", component.id.id);
}
}
