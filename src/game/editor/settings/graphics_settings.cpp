//
// Created by William on 2026-06-13.
//

#include "graphics_settings.h"

#include <cstring>
#include <glm/glm.hpp>

#include "imgui.h"

#include "core/containers/inline_string.h"

#include "game/editor/editor_widgets.h"

#include "game/systems/debug_system.h"
#include "game/systems/render_systems.h"
#include "game/systems/probe_bake_system.h"
#include "game/systems/ddgi_converge_boost.h"
#include "game/components/camera_components.h"
#include "game/components/core_components.h"
#include "game/components/render/reflection_probe_component.h"
#include "core/string_id.h"
#include "core/containers/arena_array.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "engine/profiles/profile_library.h"
#include "render/passes/ddgi_passes.h"
#include "render/passes/final_gather_passes.h"

#include "render/shaders/restir_features_macros.h"
#include "render/shaders/ddgi_interop.h"
#include "render/shaders/radiance_cache_interop.h"

namespace Game
{
static void SaveProjectConfigTab(Engine::EngineState* state)
{
    Engine::ProjectConfig& cfg = state->projectConfig;
    cfg.aaConfig = state->lighting.aaConfig;
    Engine::WriteProjectConfig(cfg, state->allocator);
}

static void SaveLightingTab(Engine::EngineState* state)
{
    Engine::ProjectConfig& cfg = state->projectConfig;
    if (!cfg.activeLightingProfile.IsEmpty()) {
        Engine::Profiles::SaveLightingProfile(cfg.activeLightingProfile.c_str(), Engine::Profiles::CaptureLightingProfile(*state), state->allocator);
    }
    Engine::WriteProjectConfig(cfg, state->allocator);
}

static void SavePostProcessTab(Engine::EngineState* state)
{
    Engine::ProjectConfig& cfg = state->projectConfig;
    if (!cfg.activePostProcessProfile.IsEmpty()) {
        Engine::Profiles::SavePostProcessProfile(cfg.activePostProcessProfile.c_str(), state->lighting.postProcess, state->allocator);
    }
    Engine::WriteProjectConfig(cfg, state->allocator);
}

static void DrawLightingProfiles(Engine::EngineState* state)
{
    Engine::ProjectConfig& cfg = state->projectConfig;
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Profile##lightingprofile", cfg.activeLightingProfile.IsEmpty() ? "(none)" : cfg.activeLightingProfile.c_str())) {
        Engine::Profiles::ProfileName names[Engine::Profiles::MAX_PROFILES];
        const uint32_t count = Engine::Profiles::ListLightingProfiles(names, Engine::Profiles::MAX_PROFILES);
        for (uint32_t i = 0; i < count; ++i) {
            if (ImGui::Selectable(names[i].c_str(), cfg.activeLightingProfile == names[i])) {
                cfg.activeLightingProfile = names[i];
                Engine::Profiles::LightingProfileBundle bundle = Engine::Profiles::CaptureLightingProfile(*state);
                if (Engine::Profiles::LoadLightingProfile(names[i].c_str(), bundle)) {
                    Engine::Profiles::ApplyLightingProfile(*state, bundle);
                }
                Engine::WriteProjectConfig(cfg, state->allocator);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(cfg.activeLightingProfile.IsEmpty());
    if (ImGui::Button("Delete##lightingprofile")) {
        Engine::Profiles::DeleteLightingProfile(cfg.activeLightingProfile.c_str());
        cfg.activeLightingProfile = Core::InlineString<64>();
        Engine::WriteProjectConfig(cfg, state->allocator);
    }
    ImGui::EndDisabled();

    static char lightingNewName[64] = "";
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("##lightingnewname", lightingNewName, sizeof(lightingNewName));
    ImGui::SameLine();
    if (ImGui::Button("Save As##lightingprofile") && lightingNewName[0] != '\0') {
        Engine::Profiles::SaveLightingProfile(lightingNewName, Engine::Profiles::CaptureLightingProfile(*state), state->allocator);
        cfg.activeLightingProfile = Core::InlineString<64>(lightingNewName);
        Engine::WriteProjectConfig(cfg, state->allocator);
        lightingNewName[0] = '\0';
    }
}

static void DrawPostProcessProfiles(Engine::EngineState* state)
{
    Engine::ProjectConfig& cfg = state->projectConfig;
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Profile##ppprofile", cfg.activePostProcessProfile.IsEmpty() ? "(none)" : cfg.activePostProcessProfile.c_str())) {
        Engine::Profiles::ProfileName names[Engine::Profiles::MAX_PROFILES];
        const uint32_t count = Engine::Profiles::ListPostProcessProfiles(names, Engine::Profiles::MAX_PROFILES);
        for (uint32_t i = 0; i < count; ++i) {
            if (ImGui::Selectable(names[i].c_str(), cfg.activePostProcessProfile == names[i])) {
                cfg.activePostProcessProfile = names[i];
                Engine::Profiles::LoadPostProcessProfile(names[i].c_str(), state->lighting.postProcess);
                Engine::WriteProjectConfig(cfg, state->allocator);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(cfg.activePostProcessProfile.IsEmpty());
    if (ImGui::Button("Delete##ppprofile")) {
        Engine::Profiles::DeletePostProcessProfile(cfg.activePostProcessProfile.c_str());
        cfg.activePostProcessProfile = Core::InlineString<64>();
        Engine::WriteProjectConfig(cfg, state->allocator);
    }
    ImGui::EndDisabled();

    static char ppNewName[64] = "";
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("##ppnewname", ppNewName, sizeof(ppNewName));
    ImGui::SameLine();
    if (ImGui::Button("Save As##ppprofile") && ppNewName[0] != '\0') {
        Engine::Profiles::SavePostProcessProfile(ppNewName, state->lighting.postProcess, state->allocator);
        cfg.activePostProcessProfile = Core::InlineString<64>(ppNewName);
        Engine::WriteProjectConfig(cfg, state->allocator);
        ppNewName[0] = '\0';
    }
}

void DrawProjectConfigWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (ImGui::Begin("Project Config")) {
        bool changed = false;
        if (Widgets::SaveBar("projectconfig", &state->projectConfig.bAutoSaveProjectConfig)) {
            SaveProjectConfigTab(state);
        }

        ImGui::Separator();

        if (ImGui::Checkbox("Limit FPS", &state->projectConfig.bLimitFps)) { changed = true; }
        if (state->projectConfig.bLimitFps) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            if (Widgets::SliderInt("##fps_cap", &state->projectConfig.frameLimitTarget, 15, 240)) { changed = true; }
        }

        ImGui::SeparatorText("Cameras");
        if (Widgets::SliderFloat("Game FOV##cam", &state->projectConfig.gameCameraFovDegrees, 30.0f, 120.0f, {.format = "%.0f deg"})) { changed = true; }
        if (Widgets::SliderFloat("Game Near##cam", &state->projectConfig.gameCameraNearPlane, 0.01f, 5.0f, {.format = "%.3f"})) { changed = true; }
        if (Widgets::SliderFloat("Editor FOV##cam", &state->projectConfig.editorCameraFovDegrees, 30.0f, 120.0f, {.format = "%.0f deg"})) { changed = true; }
        if (Widgets::SliderFloat("Editor Near##cam", &state->projectConfig.editorCameraNearPlane, 0.01f, 5.0f, {.format = "%.3f"})) { changed = true; }
        if (ImGui::Checkbox("Lock Game Aspect##cam", &state->projectConfig.gameCameraLockAspect)) { changed = true; }
        if (state->projectConfig.gameCameraLockAspect) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputFloat2("##game_aspect", &state->projectConfig.gameCameraAspect.x, "%.2f")) { changed = true; }
        } {
            auto editorCamView = state->registry.view<Component::TransformComponent, Component::EditorCameraTag>();
            const entt::entity editorCam = editorCamView.front();
            ImGui::Text("Presets:");
            for (int i = 0; i < Engine::MAX_CAMERA_PRESETS; ++i) {
                Engine::CameraPreset& preset = state->projectConfig.cameraPresets[i];
                ImGui::SameLine();
                ImGui::PushID(i);
                const bool bTinted = preset.bSet;
                if (bTinted) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.25f, 1.0f));
                }
                Core::InlineString<8> label;
                label.Format("%d", i + 1);
                if (ImGui::Button(label.c_str(), ImVec2(24.0f, 0.0f)) && editorCam != entt::null) {
                    auto& tf = state->registry.get<Component::TransformComponent>(editorCam);
                    if (ImGui::GetIO().KeyShift) {
                        preset.translation = tf.translation;
                        preset.rotation = tf.rotation;
                        preset.bSet = true;
                        changed = true;
                    }
                    else if (preset.bSet) {
                        tf.translation = preset.translation;
                        tf.rotation = preset.rotation;
                    }
                }
                if (bTinted) {
                    ImGui::PopStyleColor();
                }
                ImGui::PopID();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Editor camera bookmarks. Shift-click to save the current view; click to jump to a saved one. Green = occupied.");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (Widgets::SliderFloat("Render Resolution##graphics", &state->projectConfig.resolutionScale, 0.5f, 1.0f, {.format = "%.3f", .commitOnRelease = true})) { changed = true; }
        if (Widgets::SliderFloat("Reflection Probe Line Width##graphics", &state->projectConfig.reflectionProbeLineWidth, 0.005f, 0.1f, {.format = "%.3f", .tooltip = "World-space width of reflection probe debug wireframes (volume, fade shell, capture marker, stale bake volume).", .reset = true, .resetTo = 0.02})) { changed = true; }


        const char* aaModes[] = {"None", "SMAA", "TAA", "SMAA T2X", "Naive TAA", "Donut TAA"};
        int currentAA = static_cast<int>(state->lighting.aaConfig.mode);
        if (ImGui::Combo("Anti-Aliasing", &currentAA, aaModes, IM_ARRAYSIZE(aaModes))) {
            state->lighting.aaConfig.mode = static_cast<Core::AntiAliasingMode>(currentAA);
            changed = true;
        }

        const Core::AntiAliasingMode aaMode = state->lighting.aaConfig.mode;
        const bool bSMAA = aaMode == Core::AntiAliasingMode::SMAA || aaMode == Core::AntiAliasingMode::SMAAT2X;
        const bool bTAA = aaMode == Core::AntiAliasingMode::TAA || aaMode == Core::AntiAliasingMode::NaiveTAA;
        const bool bDonutTAA = aaMode == Core::AntiAliasingMode::DonutTAA;

        if (bSMAA && ImGui::CollapsingHeader("SMAA")) {
            Core::SMAAConfiguration& smaa = state->lighting.aaConfig.smaa;
            constexpr Core::SMAAConfiguration defaultSMAA{};
            const char* edgeModes[] = {"Luma", "Color", "Depth"};
            int currentMode = static_cast<int>(smaa.edgeDetectionMode);
            if (ImGui::Combo("Edge Detection##smaa", &currentMode, edgeModes, IM_ARRAYSIZE(edgeModes))) {
                smaa.edgeDetectionMode = static_cast<Core::SMAAEdgeDetectionMode>(currentMode);
                changed = true;
            }
            if (Widgets::SliderFloat("Threshold##smaa", &smaa.threshold, 0.01f, 0.5f, {.format = "%.3f"})) { changed = true; }
            if (Widgets::SliderFloat("Local Contrast Adapt.##smaa", &smaa.localContrastAdaptation, 0.5f, 4.0f, {.format = "%.2f"})) { changed = true; }
            if (Widgets::SliderInt("Max Search Steps##smaa", &smaa.maxSearchSteps, 1, 112)) { changed = true; }
            if (Widgets::SliderInt("Max Search Steps Diag##smaa", &smaa.maxSearchStepsDiag, 1, 20)) { changed = true; }
            if (ImGui::Button("Reset SMAA")) {
                smaa = defaultSMAA;
                changed = true;
            }
        }

        if (bTAA && ImGui::CollapsingHeader("TAA")) {
            Core::TAAConfiguration& taa = state->lighting.aaConfig.taa;
            constexpr Core::TAAConfiguration defaultTAA{};
            if (Widgets::SliderFloat("Base Blend Alpha##taa", &taa.baseBlendAlpha, 0.01f, 0.5f, {.format = "%.4f"})) { changed = true; }
            if (Widgets::SliderFloat("Disocclusion Threshold##taa", &taa.disocclusionThreshold, 0.001f, 0.2f, {.format = "%.3f"})) { changed = true; }
            if (Widgets::SliderFloat("Variance Gamma Luma##taa", &taa.varianceGammaLuma, 0.25f, 2.5f, {.format = "%.2f"})) { changed = true; }
            if (Widgets::SliderFloat("Variance Gamma Chroma##taa", &taa.varianceGammaChroma, 0.25f, 2.5f, {.format = "%.2f"})) { changed = true; }
            if (Widgets::SliderFloat("Firefly Suppression##taa", &taa.karisStrength, 0.0f, 4.0f, {.format = "%.2f"})) { changed = true; }
            if (Widgets::SliderFloat("Invalid History Blend##taa", &taa.invalidHistoryBlend, 0.0f, 1.0f, {.format = "%.2f"})) { changed = true; }
            if (Widgets::SliderFloat("Luma Boost Cap##taa", &taa.lumaBoostCap, 0.0f, 1.0f, {.format = "%.2f"})) { changed = true; }
            if (Widgets::SliderFloat("Grazing Turnover##taa", &taa.grazingTurnoverStrength, 0.0f, 100.0f, {.format = "%.1f"})) { changed = true; }
            if (ImGui::Button("Reset TAA")) {
                taa = defaultTAA;
                changed = true;
            }
        }

        if (bDonutTAA && ImGui::CollapsingHeader("Donut TAA")) {
            Core::DonutTAAConfiguration& donutTaa = state->lighting.aaConfig.donutTaa;
            constexpr Core::DonutTAAConfiguration defaultDonutTaa{};
            if (Widgets::SliderFloat("Clamping Factor##donuttaa", &donutTaa.clampingFactor, -1.0f, 4.0f, {.format = "%.2f"})) { changed = true; }
            if (Widgets::SliderFloat("New Frame Weight##donuttaa", &donutTaa.newFrameWeight, 0.01f, 1.0f, {.format = "%.3f"})) { changed = true; }
            if (Widgets::SliderFloat("Max Radiance (pqC)##donuttaa", &donutTaa.maxRadiance, 1.0f, 10000.0f, {.format = "%.1f"})) { changed = true; }
            if (ImGui::Checkbox("Catmull-Rom History##donuttaa", &donutTaa.bUseCatmullRom)) { changed = true; }
            if (ImGui::Checkbox("History Clamp Relax##donuttaa", &donutTaa.bUseHistoryClampRelax)) { changed = true; }
            if (ImGui::Button("Reset Donut TAA")) {
                donutTaa = defaultDonutTaa;
                changed = true;
            }
        }

        if (changed && state->projectConfig.bAutoSaveProjectConfig) {
            SaveProjectConfigTab(state);
        }
    }
    ImGui::End();
}

void DrawDebugViewWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (ImGui::Begin("Debug View")) {
        ImGui::Checkbox("Enable UI", &state->debug.bEnableUI);
        ImGui::SameLine();
        ImGui::Checkbox("Wireframe", &state->debug.render.bWireframe);

        ImGui::SeparatorText("Debug Visualizations");

        ImGui::Checkbox("Enable GPU Debug Draw", &state->debug.render.bEnableGPUDebug);
        ImGui::SameLine();
        ImGui::Checkbox("Lock##GPUDebug", &state->debug.render.bLockGPUDebug);

        if (ImGui::Checkbox("Cluster Grid##GPUDebug", &state->debug.render.bClusterGridDebug) && state->debug.render.bClusterGridDebug) {
            state->debug.render.bWorldGridDebug = false;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("FrustumBinning: screen-frustum-tied clusters. Only actually bound in ReSTIR mode (feeds the reflection pass); Default-mode shading uses World Grid instead.");
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("World Grid##GPUDebug", &state->debug.render.bWorldGridDebug) && state->debug.render.bWorldGridDebug) {
            state->debug.render.bClusterGridDebug = false;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("WorldGridBinning: camera-centered cascaded world-space grid used by Default-mode shading.");
        } {
            const char* worldGridLevelLabels[] = {"All", "0", "1", "2", "3", "4", "5", "6", "7"};
            int worldGridLevelChoice = state->debug.render.worldGridDebugLevel + 1;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::Combo("Level##WorldGridDebug", &worldGridLevelChoice, worldGridLevelLabels, static_cast<int>(std::size(worldGridLevelLabels)))) {
                state->debug.render.worldGridDebugLevel = worldGridLevelChoice - 1;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("All draws every cascade with an identification tint; picking one cascade draws only it, untinted. 0 is the finest (32m) cascade, doubling per level.");
            }
        }

        ImGui::Checkbox("Probe Preview##GPUDebug", &state->debug.bProbePreview);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Draws a sphere at every reflection probe's capture position, shaded from its cubemap (disabled probes included if their content is loaded). Specular samples the roughness-selected prefilter mip along the mirror reflection vector; Irradiance samples the diffuse mip along the normal.");
        }
        ImGui::SameLine();
        ImGui::Checkbox("Irradiance##ProbePreview", &state->debug.bProbePreviewIrradiance);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::BeginDisabled(state->debug.bProbePreviewIrradiance);
        Widgets::SliderFloat("Roughness##ProbePreview", &state->debug.probePreviewRoughness, 0.0f, 1.0f, {.format = "%.2f", .tooltip = "Roughness whose prefilter mip the preview sphere displays; matches the mapping shading uses.", .reset = true, .resetTo = 0.0});
        ImGui::EndDisabled();

        ImGui::Checkbox("Radiance Cache##GPUDebug", &state->debug.render.bRadianceCacheDebug);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Radiance cache: one solid cube per occupied hash-table cell, colored by decoded radiance (black = occupied but not yet shaded). Cube size follows the cell's LOD; shrunk slightly so neighbors don't merge.");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        Widgets::SliderFloat("Cache Exposure##GPUDebug", &state->debug.render.radianceCacheDebugExposure, 0.1f, 10.0f, {.format = "%.2f", .tooltip = "Linear exposure applied only to the radiance cache debug cubes so bright cells do not blow out to flat white. Visualization only; does not affect lighting.", .reset = true, .resetTo = 1.0}); {
            const char* bucketLabels[] = {"All", "+X", "-X", "+Y", "-Y", "+Z", "-Z"};
            int bucketChoice = state->debug.render.radianceCacheDebugBucket + 1;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::Combo("Direction##RadianceCacheDebug", &bucketChoice, bucketLabels, static_cast<int>(std::size(bucketLabels)))) {
                state->debug.render.radianceCacheDebugBucket = bucketChoice - 1;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("All draws every normal bucket; picking one draws only cells whose normal bucket matches (front/back separation only, not fine direction).");
            }
        }

        ImGui::SeparatorText("DDGI Probes");
        ImGui::Checkbox("Draw Probes##DDGIDebug", &state->debug.render.bDDGIProbeDebug);
        ImGui::SameLine();
        ImGui::Checkbox("Bounce Only##DDGIDebug", &state->debug.render.bDDGIBounceOnly);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Zero skybox radiance in the DDGI trace (feedback also disabled); anything left in the probes is one-bounce surface shading (sun + emissive + light-proxy emission at hits)");
        }
        ImGui::SameLine();
        ImGui::Checkbox("Hide Inactive##DDGIDebug", &state->debug.render.bDDGIHideInactiveProbes);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Skip classification-inactive probes in the debug view instead of drawing them flat blue.");
        }
        const char* cascadeLabels[] = {"All", "Locals", "0", "1", "2", "3"};
        int cascadeOptionCount = static_cast<int>(state->lighting.ddgi.cascadeCount) + 2;
        if (cascadeOptionCount < 3) { cascadeOptionCount = 3; }
        if (cascadeOptionCount > 6) { cascadeOptionCount = 6; }
        if (state->debug.render.ddgiProbeDebugCascade + 2 >= cascadeOptionCount) {
            state->debug.render.ddgiProbeDebugCascade = -1;
        }
        int cascadeChoice = state->debug.render.ddgiProbeDebugCascade == Render::DDGI_PROBE_DEBUG_LOCALS_ONLY ? 1 : state->debug.render.ddgiProbeDebugCascade + 2;
        if (state->debug.render.ddgiProbeDebugCascade == -1) { cascadeChoice = 0; }
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::Combo("Cascade##DDGIDebug", &cascadeChoice, cascadeLabels, cascadeOptionCount)) {
            state->debug.render.ddgiProbeDebugCascade = cascadeChoice == 0 ? -1 : cascadeChoice == 1 ? Render::DDGI_PROBE_DEBUG_LOCALS_ONLY : cascadeChoice - 2;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("All draws every entry with an identification tint (cascade 0 white, 1 red, 2 green, 3 blue; locals continue the palette). Locals draws only the resident hand-placed volumes, tinted. Picking one cascade draws only it, untinted.");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0f);
        Widgets::SliderFloat("Probe Exposure##GPUDebug", &state->debug.render.ddgiProbeDebugExposure, 0.1f, 10.0f, {.format = "%.2f", .tooltip = "Linear exposure applied only to the DDGI probe debug spheres so bright probes do not blow out to flat white. Visualization only; does not affect lighting.", .reset = true, .resetTo = 1.0}); {
            const char* probeDisplayLabels[] = {"Irradiance", "Visibility", "Placement", "Age"};
            ImGui::SetNextItemWidth(120.0f);
            ImGui::Combo("Display##DDGIDebug", &state->debug.render.ddgiProbeDebugMode, probeDisplayLabels, static_cast<int>(std::size(probeDisplayLabels)));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Visibility shows the probe's distance atlas as L1 lobes: red = mean distance relative to the miss clamp per direction, green = std/mean. A red-hot lobe pointing into the room from an exterior probe is a miss-inflated mean, which bypasses the Chebyshev occlusion test at sampling. Placement drops the shading entirely and draws flat per-volume tint, for reading probe positions and window coverage; dead (red) and inactive (blue) probes still show through. Age tints each world volume by its update count: green ramp while warming (0-16), then black to white as it ages toward the cap; camera cascades always show full age.");
            }
        }

        ImGui::SeparatorText("GI Diffuse Gather"); {
            const char* giGatherDebugLabels[] = {"Off", "Irradiance", "Tiers", "Hit Distance", "Accumulation", "Escape", "Variance Guide"};
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Combo("View##GIGatherDebug", &state->debug.render.giGatherDebugMode, giGatherDebugLabels, static_cast<int>(std::size(giGatherDebugLabels)))) {
                if (state->debug.render.giGatherDebugMode != 0) {
                    state->debug.resourceName = Core::InlineString("gi_gather_debug_target");
                    state->debug.transformationType = DebugTransformationType::None;
                    state->debug.viewAspect = Core::DebugViewAspect::None;
                }
                else if (state->debug.resourceName == "gi_gather_debug_target") {
                    state->debug.resourceName.Clear();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Final-gather view in the debug visualizer; runs the gather even when it is not applied, and lighting/lit history stay live so the screen tier behaves as in normal play. Irradiance = upscaled gather evaluated at the pixel normal. Tiers = where the first ray resolved: cyan screen, green cache, blue probe, yellow sky, red backface, magenta baked probe. Hit Distance = hitT grayscale. Accumulation = temporal counter (white = full history). Escape = first-ray classification: yellow sky miss, magenta backface, front-face hit distance green to red over 2-10m; red/yellow/magenta at interior texels means the ray left the room.");
            }
        }

        ImGui::SeparatorText("GI Deconstruct"); {
            const char* giDeconstructLabels[] = {"Off", "Cache Cell ID", "Cache Radiance", "DDGI Cheb Gate", "DDGI Mean vs Dist", "DDGI Coverage", "DDGI Irradiance", "World Volume Coverage"};
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::Combo("View##GIDeconstruct", &state->debug.render.giDeconstructMode, giDeconstructLabels, static_cast<int>(std::size(giDeconstructLabels)))) {
                if (state->debug.render.giDeconstructMode != 0) {
                    state->debug.resourceName = Core::InlineString("gi_deconstruct_target");
                    state->debug.transformationType = DebugTransformationType::None;
                    state->debug.viewAspect = Core::DebugViewAspect::None;
                }
                else if (state->debug.resourceName == "gi_deconstruct_target") {
                    state->debug.resourceName.Clear();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Per-pixel GI leak deconstruction at the primary surface. Cache Cell ID = hash color of the resolved radiance-cache cell; the same color on both sides of a wall means interior and exterior share one cell. Cache Radiance = what gather tier 1 would return (magenta = found but not servable). DDGI Cheb Gate = weight fractions: red = occlusion test bypassed with a miss-inflated mean, green = bypassed with a plausible mean, blue = test ran. Mean vs Dist = dominant probe's (mean - distance)/spacing: red = bypassed, green = tested; blue overlays std/mean. Coverage = R coverage, G confidence, B serving cascade. Irradiance = raw DDGI injection at the pixel. World Volume Coverage = volume placement aid: green means an authored volume owns the surface, yellow is its fade band, red means cascades serve it (leaky at interior corners), magenta means nothing covers it; brightness tracks coverage so starved pockets read dark.");
            }
        }

        ImGui::SeparatorText("Occlusion"); {
            ImGui::Checkbox("Inst Frustum##Cull", &state->debug.render.bCullInstanceFrustum);
            ImGui::SameLine();
            ImGui::Checkbox("Inst Contribution##Cull", &state->debug.render.bCullInstanceContribution);
            ImGui::SameLine();
            ImGui::Checkbox("Mlet Frustum##Cull", &state->debug.render.bCullMeshletFrustum);
            ImGui::SameLine();
            ImGui::Checkbox("Mlet Cone##Cull", &state->debug.render.bCullMeshletCone);
            ImGui::SameLine();
            ImGui::Checkbox("Mlet Contribution##Cull", &state->debug.render.bCullMeshletContribution);
            ImGui::Checkbox("Occlusion Culling##HiZ", &state->debug.render.bOcclusionCulling);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Two-phase Hi-Z occlusion culling of the visibility-buffer geometry pass. Phase 1 draws what was visible last frame, phase 2 re-tests everything against the fresh depth pyramid and late-draws disocclusions, so the final image is identical to no culling. Off = frustum-only.");
            }
            ImGui::SameLine();
            ImGui::Checkbox("Freeze##HiZ", &state->debug.render.bOcclusionFreeze);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Locks the visibility bits and stops phase 2, so only the frozen visible set draws. Move the camera or flip wireframe to see the culled geometry as holes. Debug only; the image is intentionally wrong while frozen.");
            }
            ImGui::SameLine();
            int hizMip = state->debug.render.hizDebugMip;
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::SliderInt("Hi-Z Mip##HiZDebug", &hizMip, -1, 11, hizMip < 0 ? "Off" : "%d")) {
                state->debug.render.hizDebugMip = hizMip;
                if (hizMip >= 0) {
                    state->debug.resourceName = Core::InlineString("hiz_debug_target");
                    state->debug.transformationType = DebugTransformationType::None;
                    state->debug.viewAspect = Core::DebugViewAspect::None;
                }
                else if (state->debug.resourceName == "hiz_debug_target") {
                    state->debug.resourceName.Clear();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Shows the occlusion Hi-Z depth pyramid at the chosen mip, nearest-upscaled, grayscale = pow(depth, 0.25) so reversed-Z far reads dark. Mip 0 is pow2-down of half render resolution; each level is a conservative min (farthest) reduce. Sky is black; higher mips should only ever get darker.");
            }
        }

        ImGui::SeparatorText("Render Target Views");

        ImGui::Text("Current Debug View: %s", state->debug.resourceName.IsEmpty() ? "None" : state->debug.resourceName.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Disable Debug View")) {
            state->debug.resourceName.Clear();
        }
        ImGui::Checkbox("V-Buffer Shade Dispatch Bucketing##DebugView", &state->debug.render.bEnableShadeDispatchBucketingVisualization);
        ImGui::SameLine();
        ImGui::Checkbox("V-Buffer Lighting Bucketing##DebugView", &state->debug.render.bEnableLightingBucketingVisualization);

        if (ImGui::CollapsingHeader("Hotkeys")) {
            const char* keyNames[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
            for (size_t i = 0; i < std::size(DEBUG_HOTKEYS); ++i) {
                ImGui::Text("%s: %s (%s)", keyNames[i], DEBUG_HOTKEYS[i].name, DEBUG_HOTKEYS[i].resourceName);
            }
        }

        auto setDebugTarget = [&](const char* name, DebugTransformationType _transform, Core::DebugViewAspect aspect) {
            if (state->debug.resourceName == name && state->debug.viewAspect == aspect && state->debug.transformationType == _transform) {
                state->debug.resourceName.Clear();
            }
            else {
                state->debug.resourceName = Core::InlineString(name);
                state->debug.transformationType = _transform;
                state->debug.viewAspect = aspect;
            }
        };

        if (ImGui::CollapsingHeader("Visibility Buffer")) {
            if (ImGui::Button("Visibility Buffer (Instance)")) setDebugTarget("visibility_target", DebugTransformationType::VisBuffInstance, Core::DebugViewAspect::None);
            if (ImGui::Button("Visibility Buffer (Meshlet)")) setDebugTarget("visibility_target", DebugTransformationType::VisBuffMeshlet, Core::DebugViewAspect::None);
            if (ImGui::Button("Visibility Buffer (Triangle)")) setDebugTarget("visibility_target", DebugTransformationType::VisBuffTriangle, Core::DebugViewAspect::None);
            if (ImGui::Button("Visibility Barycentric")) setDebugTarget("visibility_barycentric", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Visibility Derivatives")) setDebugTarget("visibility_derivatives", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Visibility Bucketing (Shading)")) setDebugTarget("visibility_target", DebugTransformationType::VisBucketShading, Core::DebugViewAspect::None);
            if (ImGui::Button("Visibility Bucketing (Lighting)")) setDebugTarget("visibility_target", DebugTransformationType::VisBucketLighting, Core::DebugViewAspect::None);
        }
        if (ImGui::CollapsingHeader("ReSTIR DI Visualize")) {
            if (ImGui::Button("Generate Light Index")) setDebugTarget("depth_target", DebugTransformationType::ReservoirLightIdx, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Generate W")) setDebugTarget("depth_target", DebugTransformationType::ReservoirGenerateW, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Temporal Light Index")) setDebugTarget("depth_target", DebugTransformationType::ReservoirTemporalLightIdx, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Temporal W")) setDebugTarget("depth_target", DebugTransformationType::ReservoirTemporalW, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Spatial Light Index")) setDebugTarget("depth_target", DebugTransformationType::ReservoirSpatialLightIdx, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Spatial W")) setDebugTarget("depth_target", DebugTransformationType::ReservoirSpatialW, Core::DebugViewAspect::Depth);
            if (ImGui::Button("History Light Index")) setDebugTarget("depth_target", DebugTransformationType::ReservoirHistoryLightIdx, Core::DebugViewAspect::Depth);
            if (ImGui::Button("History W")) setDebugTarget("depth_target", DebugTransformationType::ReservoirHistoryW, Core::DebugViewAspect::Depth);
        }
        if (ImGui::CollapsingHeader("Reflections")) {
            if (ImGui::Button("Raw Traced (Demodulated)")) setDebugTarget("reflection_spec_noisy", DebugTransformationType::None, Core::DebugViewAspect::None);
        }
        if (ImGui::CollapsingHeader("Reflection Probes")) {
            if (ImGui::Button("Reflection Probe Index")) setDebugTarget("depth_target", DebugTransformationType::ReflectionProbeIndex, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Probe Bin Disagreement")) setDebugTarget("depth_target", DebugTransformationType::ReflectionProbeBinDisagreement, Core::DebugViewAspect::Depth);
        }
        if (ImGui::CollapsingHeader("G-Buffer")) {
            if (ImGui::Button("Depth")) setDebugTarget("depth_target", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            ImGui::SameLine();
            if (ImGui::Button("Stencil")) setDebugTarget("depth_target", DebugTransformationType::StencilRemap, Core::DebugViewAspect::Stencil);

            if (ImGui::Button("Albedo")) setDebugTarget("gbuffer_two", DebugTransformationType::GBufferAlbedo, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Normal")) setDebugTarget("gbuffer_one", DebugTransformationType::GBufferNormal, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("PBR")) setDebugTarget("gbuffer_one", DebugTransformationType::GBufferPBR, Core::DebugViewAspect::None);

            if (ImGui::Button("Emissive")) setDebugTarget("gbuffer_two", DebugTransformationType::GBufferEmissive, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Motion Vectors")) setDebugTarget("gbuffer_one", DebugTransformationType::GBufferMotionVectors, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("View Z Delta")) setDebugTarget("gbuffer_one", DebugTransformationType::GBufferViewZDelta, Core::DebugViewAspect::None);

            if (ImGui::Button("Intermediate One (Diffuse)")) setDebugTarget("intermediate_one", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Intermediate Two (Specular)")) setDebugTarget("intermediate_two", DebugTransformationType::None, Core::DebugViewAspect::None);

            if (ImGui::Button("View Space Position")) setDebugTarget("depth_target", DebugTransformationType::ViewSpacePosition, Core::DebugViewAspect::Depth);
            ImGui::SameLine();
            if (ImGui::Button("NdotV")) setDebugTarget("gbuffer_one", DebugTransformationType::NdotV, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Lighting")) {
            if (ImGui::Button("Shading Output")) setDebugTarget("shading_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Depth")) setDebugTarget("gtao_depth", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO AO")) setDebugTarget("gtao_ao", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Edges")) setDebugTarget("gtao_edges", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Filtered")) setDebugTarget("gtao_filtered", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Bent Normals")) setDebugTarget("gtao_bent_normals", DebugTransformationType::BentNormal, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("SIGMA")) {
            if (ImGui::Button("Trace Visibility")) setDebugTarget("rt_sun_shadow", DebugTransformationType::SunShadowVisibility, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Trace Penumbra")) setDebugTarget("rt_sun_shadow", DebugTransformationType::SunShadowPenumbra, Core::DebugViewAspect::None);

            if (ImGui::Button("Tiles (Classify)")) setDebugTarget("sigma_tiles", DebugTransformationType::SunShadowTiles, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Tiles (Smoothed)")) setDebugTarget("sigma_tiles_smoothed", DebugTransformationType::SunShadowTileValue, Core::DebugViewAspect::None);

            if (ImGui::Button("Blur Visibility")) setDebugTarget("sigma_shadow", DebugTransformationType::SunShadowVisibility, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Blur Penumbra")) setDebugTarget("sigma_shadow", DebugTransformationType::SunShadowPenumbra, Core::DebugViewAspect::None);

            if (ImGui::Button("Post-Blur Visibility")) setDebugTarget("sigma_shadow_2", DebugTransformationType::SunShadowVisibility, Core::DebugViewAspect::None);

            if (ImGui::Button("Stabilized Visibility")) setDebugTarget("sigma_stabilized", DebugTransformationType::SunShadowVisibility, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Stabilized Penumbra")) setDebugTarget("sigma_stabilized", DebugTransformationType::SunShadowPenumbra, Core::DebugViewAspect::None);
        }


        if (ImGui::CollapsingHeader("ReBLUR")) {
            if (ImGui::Button("Frames (DATA1)")) setDebugTarget("reblur_data1", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Carried Frames")) setDebugTarget("reblur_internal_data", DebugTransformationType::ReblurInternalData, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Occlusion+VHA")) setDebugTarget("reblur_data2", DebugTransformationType::ReblurData2, Core::DebugViewAspect::None);

            if (ImGui::Button("Packed Diff")) setDebugTarget("reblur_diff_packed", DebugTransformationType::YCoCgSignal, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Packed Spec")) setDebugTarget("reblur_spec_packed", DebugTransformationType::YCoCgSignal, Core::DebugViewAspect::None);

            if (ImGui::Button("Accum Diff")) setDebugTarget("reblur_diff_accum", DebugTransformationType::YCoCgSignal, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Accum Spec")) setDebugTarget("reblur_spec_accum", DebugTransformationType::YCoCgSignal, Core::DebugViewAspect::None);

            if (ImGui::Button("HistoryFix Diff")) setDebugTarget("reblur_diff_hfix", DebugTransformationType::YCoCgSignal, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("HistoryFix Spec")) setDebugTarget("reblur_spec_hfix", DebugTransformationType::YCoCgSignal, Core::DebugViewAspect::None);

            if (ImGui::Button("Blur Diff")) setDebugTarget("reblur_diff_blur", DebugTransformationType::YCoCgSignal, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Blur Spec")) setDebugTarget("reblur_spec_blur", DebugTransformationType::YCoCgSignal, Core::DebugViewAspect::None);

            if (ImGui::Button("PostBlur Diff")) setDebugTarget("reblur_diff_hist", DebugTransformationType::YCoCgSignal, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("PostBlur Spec")) setDebugTarget("reblur_spec_hist", DebugTransformationType::YCoCgSignal, Core::DebugViewAspect::None);

            if (ImGui::Button("Fast Diff")) setDebugTarget("reblur_diff_fast_fixed", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Fast Spec")) setDebugTarget("reblur_spec_fast_fixed", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Spec HitDist")) setDebugTarget("reblur_spec_hit_dist", DebugTransformationType::None, Core::DebugViewAspect::None);

            if (ImGui::Button("Luma Stab Diff")) setDebugTarget("reblur_diff_luma_stab", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Luma Stab Spec")) setDebugTarget("reblur_spec_luma_stab", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Anti-Aliasing")) {
            if (ImGui::Button("TAA Current")) setDebugTarget("taa_current", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("TAA Output")) setDebugTarget("taa_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::Separator();
            if (ImGui::Button("SMAA Edges")) setDebugTarget("smaa_edges", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("SMAA Blend Weights")) setDebugTarget("smaa_blend", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("SMAA Output")) setDebugTarget("smaa_output", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("RELAX Denoiser")) {
            // Tiles
            if (ImGui::Button("Tiles")) setDebugTarget("relax_tiles", DebugTransformationType::None, Core::DebugViewAspect::None);
            // Prepass
            if (ImGui::Button("Spec Prepass")) setDebugTarget("relax_spec_prepass", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Diff Prepass")) setDebugTarget("relax_diff_prepass", DebugTransformationType::None, Core::DebugViewAspect::None);

            if (ImGui::Button("Spec Illum")) setDebugTarget("relax_spec_illum", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Diff Illum")) setDebugTarget("relax_diff_illum", DebugTransformationType::None, Core::DebugViewAspect::None);

            if (ImGui::Button("Spec Illum Hist")) setDebugTarget("relax_spec_illum_history", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Diff Illum Hist")) setDebugTarget("relax_diff_illum_history", DebugTransformationType::None, Core::DebugViewAspect::None);

            if (ImGui::Button("Spec Fast")) setDebugTarget("relax_spec_fast", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Diff Fast")) setDebugTarget("relax_diff_fast", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("History Length")) setDebugTarget("relax_history_length", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Spec Hit Dist")) setDebugTarget("relax_spec_hit_dist", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Reproj Confidence")) setDebugTarget("relax_spec_reproj_confidence", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("History Confidence (ReSTIR)")) setDebugTarget("restir_confidence", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Shadow Vis (ReSTIR)")) setDebugTarget("restir_shadow_vis", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Signal (ReSTIR)")) setDebugTarget("restir_signal", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Gradient (ReSTIR)")) setDebugTarget("restir_gradient", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Prev NR")) setDebugTarget("relax_prev_nr", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("ATrous Spec 0")) setDebugTarget("relax_atrous_spec_0", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Spec History")) setDebugTarget("relax_spec_hist", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("ATrous Diff 0")) setDebugTarget("relax_atrous_diff_0", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Diff History")) setDebugTarget("relax_diff_hist", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Post-Processing")) {
            if (ImGui::Button("Bloom Chain")) setDebugTarget("bloom_chain", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Motion Blur Velocity")) setDebugTarget("motion_blur_velocity", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Motion Blur Tiled Max")) setDebugTarget("motion_blur_tiled_max", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Motion Blur Neighbor Max")) setDebugTarget("motion_blur_tiled_neighbor_max", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Motion Blur Output")) setDebugTarget("motion_blur_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Finalize Output")) setDebugTarget("tonemap_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Post Process Output")) setDebugTarget("post_process_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Screen Fade Output")) setDebugTarget("screen_fade_output", DebugTransformationType::None, Core::DebugViewAspect::None);
        }
    }
    ImGui::End();
}

static void DrawRELAXParamsUI(bool& changed, Core::RELAXParams& relax, const char* idScope, bool bShowChromaAtrous, bool bNrdMode = false)
{
    ImGui::PushID(idScope);

    static const Core::RELAXParams relaxDefaults{};
    auto relaxTip = [&](const char* tip) {
        if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tip);
        }
    };
    auto relaxF = [&](const char* label, float* v, float def, float mn, float mx, const char* fmt = "%.4f", const char* tip = nullptr) {
        changed |= Widgets::SliderFloat(label, v, mn, mx, {.format = fmt, .tooltip = tip, .reset = true, .resetTo = def});
    };
    auto relaxI = [&](const char* label, int* v, int def, int mn, int mx, const char* tip = nullptr) {
        changed |= Widgets::SliderInt(label, v, mn, mx, {.tooltip = tip, .reset = true, .resetTo = static_cast<double>(def)});
    };

    if (ImGui::Checkbox("Prepass##relax", &relax.enablePrepass)) { changed = true; }
    relaxTip("Spatial pre-blur before temporal accumulation, lowering the input noise fed into history. Default on.");
    ImGui::SameLine();
    if (ImGui::Checkbox("Anti-Firefly##relax", &relax.enableAntiFirefly)) { changed = true; }
    relaxTip("Suppresses isolated bright outlier pixels (fireflies) before accumulation. Default on.");
    if (ImGui::Checkbox("Roughness Edge Stopping##relax", &relax.roughnessEdgeStoppingEnabled)) { changed = true; }
    relaxTip("Roughness-aware specular edge stopping (roughness + oriented-normal weights). Off uses a simpler normal-only weight. Default on.");

    ImGui::SeparatorText("General");
    relaxF("Denoising Range", &relax.denoisingRange, relaxDefaults.denoisingRange, 10.f, 5000.f, "%.1f", "Max view-space distance (world units) that gets denoised; farther surfaces pass through untouched. Default 1000; set to roughly cover your scene depth.");
    relaxF("Disocclusion Threshold", &relax.disocclusionThreshold, relaxDefaults.disocclusionThreshold, 0.001f, 0.05f, "%.4f", "Relative depth tolerance for accepting reprojected history. Higher accepts more (less ghosting rejection); lower resets more on edges/motion. A jitter/1px depth bonus is added on top. Default 0.01.");
    relaxF("Depth Threshold", &relax.depthThreshold, relaxDefaults.depthThreshold, 0.0f, 0.05f, "%.4f", "Plane-distance tolerance for spatial edge stopping, as a fraction of depth. Lower preserves geometry edges; higher blurs across them. Default 0.003.");

    ImGui::SeparatorText("Accumulation");
    relaxF("Spec Max Accum Frames", &relax.specMaxAccumFrames, relaxDefaults.specMaxAccumFrames, 0.f, 64.f, "%.0f", "Max specular history length (stable). Higher = cleaner but laggier reflections. Default 32; common 30-60.");
    relaxF("Spec Max Fast Accum Frames", &relax.specMaxFastAccumFrames, relaxDefaults.specMaxFastAccumFrames, 0.f, 16.f, "%.0f", "Length of the noisy 'fast' specular history used to clamp the slow one (anti-lag). Must be below Spec Max Accum to enable clamping. NRD default 6 (~5x below main). Default 6.");
    relaxF("Diff Max Accum Frames", &relax.diffMaxAccumFrames, relaxDefaults.diffMaxAccumFrames, 0.f, 64.f, "%.0f", "Max diffuse history length (stable). Higher = cleaner but slower to react to lighting changes (more lag). Default 32; common 30-60.");
    relaxF("Diff Max Fast Accum Frames", &relax.diffMaxFastAccumFrames, relaxDefaults.diffMaxFastAccumFrames, 0.f, 16.f, "%.0f", "Length of the noisy 'fast' diffuse history used to clamp the slow one (anti-lag). Lower = snappier response. Must be below Diff Max Accum. NRD default 6. Default 6.");
    relaxF("History Acceleration Amount", &relax.historyAccelerationAmount, relaxDefaults.historyAccelerationAmount, 0.f, 1.f, "%.2f", "Strength of anti-lag acceleration pushing the slow history toward the fast one on changes. 0 = off, 1 = max. Default 1.0.");

    ImGui::SeparatorText("Prepass");
    relaxF("Diff Blur Radius", &relax.diffBlurRadius, relaxDefaults.diffBlurRadius, 0.f, 100.f, "%.1f", "Radius (px) of the diffuse pre-blur applied before accumulation. Larger knocks down more input noise but loses detail. 0 disables. Default 30.");
    relaxF("Spec Blur Radius", &relax.specBlurRadius, relaxDefaults.specBlurRadius, 0.f, 100.f, "%.1f", "Radius (px) of the specular pre-blur before accumulation. 0 disables. Default 50.");
    relaxF("Min Hit Distance Weight", &relax.minHitDistanceWeight, relaxDefaults.minHitDistanceWeight, 0.f, 1.f, "%.2f", "Minimum weight for ray hit-distance when reconstructing specular in the prepass. 0 ignores hitT. Default 0; NRD commonly ~0.1-0.2.");

    ImGui::SeparatorText("A-Trous / Edge Stopping");
    relaxI("ATrous Iterations", &relax.atrousIterations, relaxDefaults.atrousIterations, 2, 8, "Number of A-trous wavelet (spatial) passes; step doubles each pass (reach ~2^n px). More = wider denoising, costlier. NRD default 5. Default 5.");
    if (bShowChromaAtrous) {
        ImGui::BeginDisabled(bNrdMode);
        if (ImGui::Checkbox("Chroma Widening##relax", &relax.bChromaAtrous)) { changed = true; }
        relaxTip("Extra diffuse-only passes filtering chroma (CoCg chromaticity) with geometric weights only; luminance untouched. Targets low-frequency hue blotches from spatially-reused light selection, which sit past the main chain's reach. Default on.");
        relaxI("Chroma Widening Passes", &relax.chromaAtrousIterations, relaxDefaults.chromaAtrousIterations, 1, 4, "Chroma pass count; strides 32/64/128/256, so each added pass doubles the hue-smoothing reach. Default 2.");
        relaxF("Chroma Luma Ratio Power", &relax.chromaLumaPower, relaxDefaults.chromaLumaPower, 0.f, 6.f, "%.2f",
               "Falloff on the tap/center luminance ratio. Chroma taps carry no luminance and every other weight here is geometric, so without this a cast shadow (same plane, same normal) takes the lit side's hue as a colored halo. 0 = off (pre-2026-07-30 behaviour). Integer values compile to a multiply chain. Default 2.");
        ImGui::EndDisabled();
    }
    relaxF("Lobe Angle Fraction", &relax.lobeAngleFraction, relaxDefaults.lobeAngleFraction, 0.f, 1.f, "%.3f", "Normal edge-stopping tolerance, as a fraction of the BRDF lobe angle. Lower preserves sharper normal detail; higher blurs across normals. Default 0.15.");
    relaxF("Roughness Fraction", &relax.roughnessFraction, relaxDefaults.roughnessFraction, 0.f, 1.f, "%.3f", "Roughness edge-stopping tolerance (fraction). Higher blends across differing roughness; lower keeps roughness boundaries crisp. Default 0.15.");
    relaxF("Spec Lobe Angle Slack", &relax.specLobeAngleSlack, relaxDefaults.specLobeAngleSlack, 0.f, 1.f, "%.3f", "Extra angular slack added to the specular lobe for edge stopping, loosening normal/view rejection. Default 0.15.");
    relaxF("Spec Phi Luminance", &relax.specPhiLuminance, relaxDefaults.specPhiLuminance, 0.f, 10.f, "%.2f", "Specular luminance edge-stopping sensitivity (sigma scale). Higher = more blur (ignores luminance diffs); lower preserves highlights. NRD default 1.0. Default 1.0.");
    relaxF("Diff Phi Luminance", &relax.diffPhiLuminance, relaxDefaults.diffPhiLuminance, 0.f, 10.f, "%.2f", "Diffuse luminance edge-stopping sensitivity (sigma scale). Higher = more blur; lower keeps luminance edges. Default 2.0; common 1-2.");
    relaxF("Diff Max Lum Rel Diff", &relax.diffMaxLuminanceRelativeDifference, relaxDefaults.diffMaxLuminanceRelativeDifference, 0.f, 100.f, "%.2f", "Caps how strongly a luminance difference can reject a diffuse sample (in sigmas). Lower = firmer edge stopping (blotchier fireflies); NRD default is uncapped. Default 100.");
    relaxF("Spec Max Lum Rel Diff", &relax.specMaxLuminanceRelativeDifference, relaxDefaults.specMaxLuminanceRelativeDifference, 0.f, 100.f, "%.2f", "Caps how strongly a luminance difference can reject a specular sample (in sigmas). NRD default is uncapped. Default 100.");
    // NRD 4.17.4 uploads roughnessEdgeStoppingRelaxation in this field's place (upstream bug), so the knob is inert there
    ImGui::BeginDisabled(bNrdMode);
    relaxF("Luminance Edge Stop Relax", &relax.luminanceEdgeStoppingRelaxation, relaxDefaults.luminanceEdgeStoppingRelaxation, 0.f, 1.f, "%.2f", "On early A-trous passes, relaxes specular luminance edge stopping where reprojection confidence is low (helps fresh/disoccluded pixels). 1 = full NRD fill at zero confidence. Default 1.0.");
    ImGui::EndDisabled();
    relaxF("Normal Edge Stop Relax", &relax.normalEdgeStoppingRelaxation, relaxDefaults.normalEdgeStoppingRelaxation, 0.f, 1.f, "%.2f", "Relaxes specular normal edge stopping based on reprojection confidence, cutting noise on low-confidence pixels. 0-1. Default 0.3.");
    relaxF("Roughness Edge Stop Relax", &relax.roughnessEdgeStoppingRelaxation, relaxDefaults.roughnessEdgeStoppingRelaxation, 0.f, 1.f, "%.2f", "Relaxes the view vector used in specular weighting, loosening rejection on curved/rough surfaces. NRD effective default 1.0. Default 1.0.");
    relaxF("Spec Variance Boost", &relax.specVarianceBoost, relaxDefaults.specVarianceBoost, 0.f, 8.f, "%.2f", "Boosts specular variance while history is short so fresh pixels filter more aggressively. 0 = no boost (NRD default). Default 0.0.");

    ImGui::SeparatorText("History Fix");
    relaxF("Hist Fix Edge Stop Normal Pow", &relax.historyFixEdgeStoppingNormalPower, relaxDefaults.historyFixEdgeStoppingNormalPower, 0.f, 32.f, "%.1f", "Normal-match strictness for the history-fix fill that bootstraps fresh pixels. Higher = stricter normal matching. Default 8.");
    relaxF("Hist Fix Frame Num", &relax.historyFixFrameNum, relaxDefaults.historyFixFrameNum, 0.f, 32.f, "%.1f", "Pixels with history shorter than this get a sparse spatial fill (bootstrap) instead of relying on accumulation. 0 disables. Default 4.");
    relaxF("Hist Fix Base Pixel Stride", &relax.historyFixBasePixelStride, relaxDefaults.historyFixBasePixelStride, 0.f, 32.f, "%.1f", "Base sample spacing (px) for the history-fix fill; shrinks as history grows. Larger = wider initial fill. Default 14.");

    ImGui::SeparatorText("History Clamp / Reset");
    relaxF("Fast History Clamp Sigma", &relax.fastHistoryClampingSigmaScale, relaxDefaults.fastHistoryClampingSigmaScale, 0.f, 8.f, "%.2f", "Width (in sigmas) of the fast-history color box that clamps the slow history (anti-lag/anti-ghosting). Lower = tighter clamp, less lag but more noise. Default 2.0; common 1-2.");
    relaxF("History Reset Temporal Sigma", &relax.historyResetTemporalSigmaScale, relaxDefaults.historyResetTemporalSigmaScale, 0.f, 10.f, "%.2f", "Temporal noise sigma scale in history-reset detection; larger tolerates more temporal noise before resetting. Default 5.");
    relaxF("History Reset Spatial Sigma", &relax.historyResetSpatialSigmaScale, relaxDefaults.historyResetSpatialSigmaScale, 0.f, 10.f, "%.2f", "Spatial noise sigma scale in history-reset detection; larger tolerates more spatial noise before resetting. Default 1.");
    relaxF("History Reset Amount", &relax.historyResetAmount, relaxDefaults.historyResetAmount, 0.f, 1.f, "%.2f", "How hard to snap history to the current noisy signal on big lighting changes. 0 = off (rely on clamping); 1 = aggressive. Default 0.5.");

    ImGui::Spacing();
    if (ImGui::Button("Reset RELAX")) {
        relax = Core::RELAXParams{};
        changed = true;
    }

    ImGui::PopID();
}

static void DrawReBLURParamsUI(bool& changed, Core::ReBLURParams& reblur, bool bNrdMode = false)
{
    static const Core::ReBLURParams reblurDefaults{};
    auto reblurF = [&](const char* label, float* v, float def, float mn, float mx, const char* fmt = "%.4f", const char* tip = nullptr) {
        changed |= Widgets::SliderFloat(label, v, mn, mx, {.format = fmt, .tooltip = tip, .reset = true, .resetTo = def});
    };
    auto reblurI = [&](const char* label, int* v, int def, int mn, int mx, const char* tip = nullptr) {
        changed |= Widgets::SliderInt(label, v, mn, mx, {.tooltip = tip, .reset = true, .resetTo = static_cast<double>(def)});
    };

    if (ImGui::Checkbox("Prepass##reblur", &reblur.enablePrepass)) { changed = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox("Anti-Firefly##reblur", &reblur.enableAntiFirefly)) { changed = true; }
    if (ImGui::Checkbox("Temporal Stabilization##reblur", &reblur.enableTemporalStabilization)) { changed = true; }
    ImGui::SameLine();
    // No NRD counterpart (engine extension)
    ImGui::BeginDisabled(bNrdMode);
    if (ImGui::Checkbox("Stab. Firefly Cleanup##reblur", &reblur.enableStabilizationFireflyCleanup)) { changed = true; }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("NRD short-history luma cap. Eats sparse disoccluded-pixel energy (black band on fast camera motion); keep OFF."); }
    ImGui::EndDisabled();

    ImGui::SeparatorText("General");
    reblurF("Denoising Range", &reblur.denoisingRange, reblurDefaults.denoisingRange, 10.f, 5000.f, "%.1f", "Max view-space distance (world units) that gets denoised; farther surfaces pass through. Default 1000.");
    reblurF("Disocclusion Threshold", &reblur.disocclusionThreshold, reblurDefaults.disocclusionThreshold, 0.001f, 0.05f, "%.4f", "Relative depth tolerance for accepting reprojected history. Default 0.01.");
    reblurF("Plane Distance Sensitivity", &reblur.planeDistanceSensitivity, reblurDefaults.planeDistanceSensitivity, 0.001f, 0.2f, "%.4f", "Max allowed deviation from the local tangent plane for spatial edge stopping. Default 0.02.");
    reblurF("Lobe Angle Fraction", &reblur.lobeAngleFraction, reblurDefaults.lobeAngleFraction, 0.f, 1.f, "%.3f", "Normal edge-stopping tolerance as a fraction of the BRDF lobe angle. Default 0.15.");
    reblurF("Roughness Fraction", &reblur.roughnessFraction, reblurDefaults.roughnessFraction, 0.f, 1.f, "%.3f", "Roughness edge-stopping tolerance (fraction). Default 0.15.");
    reblurF("Min Hit Distance Weight", &reblur.minHitDistanceWeight, reblurDefaults.minHitDistanceWeight, 0.f, 0.2f, "%.3f", "Sensitivity to hit distance in spatial passes; smaller for clean RTXDI-style hitT. Default 0.1.");

    ImGui::SeparatorText("Hit Distance Normalization (A/B/C/D)");
    reblurF("Hit Dist A", &reblur.hitDistA, reblurDefaults.hitDistA, 0.f, 50.f, "%.2f", "Constant term (units). Default 3.");
    reblurF("Hit Dist B", &reblur.hitDistB, reblurDefaults.hitDistB, 0.f, 5.f, "%.3f", "viewZ-based linear scale. Default 0.1.");
    reblurF("Hit Dist C", &reblur.hitDistC, reblurDefaults.hitDistC, 1.f, 100.f, "%.1f", "Roughness-based scale (>1 = larger hit distance for low roughness). Default 20.");

    ImGui::BeginDisabled(bNrdMode);
    reblurF("Hit Dist D", &reblur.hitDistD, reblurDefaults.hitDistD, -50.f, 0.f, "%.1f", "Roughness falloff exponent (<=0). Default -25.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Accumulation");
    reblurF("Max Accum Frames", &reblur.maxAccumulatedFrameNum, reblurDefaults.maxAccumulatedFrameNum, 0.f, 63.f, "%.0f", "Max (stable) history length. Higher = cleaner but laggier. Default 30.");
    reblurF("Max Fast Accum Frames", &reblur.maxFastAccumulatedFrameNum, reblurDefaults.maxFastAccumulatedFrameNum, 0.f, 32.f, "%.0f", "Fast (responsive) history length used for anti-lag clamping. Usually ~1/6 of max. Default 6.");
    reblurF("Max Stabilized Frames", &reblur.maxStabilizedFrameNum, reblurDefaults.maxStabilizedFrameNum, 0.f, 63.f, "%.0f", "History length for the temporal stabilization pass. 0 disables stabilization. Default 30.");

    ImGui::SeparatorText("Blur");
    reblurF("Min Blur Radius", &reblur.minBlurRadius, reblurDefaults.minBlurRadius, 0.f, 10.f, "%.2f", "Min denoising radius (px) for the converged state. Default 1.");
    reblurF("Max Blur Radius", &reblur.maxBlurRadius, reblurDefaults.maxBlurRadius, 0.f, 60.f, "%.1f", "Base (max) denoising radius (px); shrinks as history grows. Default 30.");
    reblurF("Diffuse Prepass Blur Radius", &reblur.diffusePrepassBlurRadius, reblurDefaults.diffusePrepassBlurRadius, 0.f, 100.f, "%.1f", "Diffuse pre-blur radius (px). 0 disables. Default 30.");
    reblurF("Specular Prepass Blur Radius", &reblur.specularPrepassBlurRadius, reblurDefaults.specularPrepassBlurRadius, 0.f, 100.f, "%.1f", "Specular pre-blur radius (px). 0 disables. Default 50.");

    ImGui::BeginDisabled(bNrdMode);
    if (ImGui::Checkbox("Chroma Widening##reblur", &reblur.bChromaAtrous)) { changed = true; }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("Extra diffuse-only passes filtering chroma (CoCg chromaticity) with geometric weights only; luminance untouched. Targets low-frequency hue blotches from spatially-reused light selection, which sit past the main chain's reach. Default on."); }
    reblurI("Chroma Widening Passes", &reblur.chromaAtrousIterations, reblurDefaults.chromaAtrousIterations, 1, 4, "Chroma pass count; strides 32/64/128/256, so each added pass doubles the hue-smoothing reach. Default 2.");
    reblurF("Chroma Luma Ratio Power", &reblur.chromaLumaPower, reblurDefaults.chromaLumaPower, 0.f, 6.f, "%.2f",
            "Falloff on the tap/center luminance ratio. Chroma taps carry no luminance and every other weight here is geometric, so without this a cast shadow (same plane, same normal) takes the lit side's hue as a colored halo. 0 = off. Integer values compile to a multiply chain. Default 2.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("History Fix");
    reblurF("Hist Fix Frame Num", &reblur.historyFixFrameNum, reblurDefaults.historyFixFrameNum, 0.f, 32.f, "%.1f", "Pixels with history shorter than this get a sparse spatial fill. Default 3.");
    reblurF("Hist Fix Base Pixel Stride", &reblur.historyFixBasePixelStride, reblurDefaults.historyFixBasePixelStride, 0.f, 32.f, "%.1f", "Base sample spacing (px) for the history-fix fill; shrinks as history grows. Default 14.");
    reblurF("Fast History Clamp Sigma", &reblur.fastHistoryClampingSigmaScale, reblurDefaults.fastHistoryClampingSigmaScale, 1.f, 3.f, "%.2f", "Width (sigmas) of the fast-history color box clamping the slow history. Default 2.");

    ImGui::SeparatorText("Stabilization / Antilag");

    ImGui::BeginDisabled(bNrdMode);
    reblurF("Stabilization Strength", &reblur.stabilizationStrength, reblurDefaults.stabilizationStrength, 0.f, 1.f, "%.2f", "Blend toward the reprojected stabilized history. 0 = off. Default 1.");
    ImGui::EndDisabled();
    reblurF("Antilag Luminance Sigma", &reblur.antilagLuminanceSigmaScale, reblurDefaults.antilagLuminanceSigmaScale, 1.f, 5.f, "%.2f", "Color-box width (sigmas) used to clamp the stabilized history. Lower = tighter, less lag. Default 2.");
    reblurF("Firefly Suppressor Min Scale", &reblur.fireflySuppressorMinRelativeScale, reblurDefaults.fireflySuppressorMinRelativeScale, 1.f, 3.f, "%.2f", "Outlier suppression strength (smaller = stronger). Default 2.");

    ImGui::Spacing();
    if (ImGui::Button("Reset ReBLUR")) {
        reblur = Core::ReBLURParams{};
        changed = true;
    }
}

static void DrawProbeBakeSection(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (ImGui::Button("Bake All Probes##probebakeall")) {
        ProbeBakeGet(ctx).EnqueueAllProbes(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Bake All (2-Pass Interbounce)##probebakeall2")) {
        ProbeBakeGet(ctx).EnqueueAllProbesInterbounce(state);
    }

    ProbeBakeSystem* bake = &ProbeBakeGet(ctx);
    const bool bakeActive = bake->bBakeActive;
    if (bakeActive || !bake->bakeQueue.IsEmpty() || bake->bInterbounceBatch) {
        if (bake->bInterbounceBatch) {
            ImGui::Text("Pass %d/2", bake->bakePass);
        }
        if (bake->phase == ProbeBakeSystem::Phase::AwaitAssembles) {
            ImGui::Text("Waiting for probe assembles (%u remaining)", static_cast<uint32_t>(bake->awaitedAssembles.Size()));
        }
        if (bake->bakeBatchTotal > 0) {
            const uint32_t remaining = static_cast<uint32_t>(bake->bakeQueue.Size());
            const uint32_t done = bake->bakeBatchTotal > remaining ? bake->bakeBatchTotal - remaining : 0;
            ImGui::Text("Baking probe %u of %u", done, bake->bakeBatchTotal);
        }
        if (bakeActive) {
            ImGui::Text(bake->bDryRun ? "Face %d/6, settle frame %d/%d (dry run)" : "Face %d/6, settle frame %d/%d", bake->currentFace + 1, bake->settleCounter, bake->settleFrames);
        }
        if (ImGui::Button("Cancel Bake##probebakecancel")) {
            bake->Cancel(ctx, state);
        }
    }

    ImGui::SeparatorText("Settings");
    Engine::ProbeBakeSettings& probeBake = state->projectConfig.probeBake;
    bool bakeChanged = false;
    if (Widgets::SliderInt("Settle Frames##bake", &probeBake.settleFrames, 1, 1024, {.tooltip = "Rendered frames held on each cube face before its capture snapshot; covers static-camera TAA convergence plus the corner-leak disocclusion transient. Default 240.", .reset = true, .resetTo = 240.0})) { bakeChanged = true; }
    if (Widgets::SliderInt("Capture Size##bake", &probeBake.captureSize, 256, 1280, {.tooltip = "Per-face capture resolution in pixels before downsample to the probe resolution; larger values cost bake time only. Default 1024.", .reset = true, .resetTo = 1024.0})) { bakeChanged = true; }
    if (ImGui::Checkbox("Converge Before Capture##bake", &probeBake.bAutoConverge)) { bakeChanged = true; }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Run the DDGI converge boost once per probe; the first face's capture waits for it to finish on top of the settle.");
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Freeze During Capture##bake", &probeBake.bAutoFreeze)) { bakeChanged = true; }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Engage GI Freeze once the first face has converged and settled, so all 6 faces capture the identical field. Freeze scope follows the checkboxes below. Restored when the probe finishes.");
    }
    if (ImGui::Checkbox("Ground Truth Bake##bake", &probeBake.bGroundTruth)) { bakeChanged = true; }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Bake faces with the Full ground-truth path tracer instead of the live lighting path. Total samples per face = Settle Frames x GT Samples/Frame; the converge boost and GI freeze are skipped. The GI-gather-off / GTAO-off profile hygiene is unnecessary in this mode.");
    }
    if (Widgets::SliderInt("GT Samples/Frame##bake", &probeBake.groundTruthSpp, 1, 32, {.tooltip = "Path-traced samples per pixel per frame during a Ground Truth bake. Higher converges each face in fewer frames at the same total cost; too high risks GPU timeout at large capture sizes. Default 8.", .reset = true, .resetTo = 8.0})) { bakeChanged = true; }
    if (bakeChanged) {
        Engine::WriteProjectConfig(state->projectConfig, state->allocator);
    }

    ImGui::Checkbox("Freeze##bake", &state->debug.bGIFreeze);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Manually halts the checked GI stages and carries their state unchanged; sampling keeps reading the frozen data. Converge Now unfreezes. The bake engages this automatically per probe when Freeze During Capture is on.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Probes & Cache##freeze", &state->debug.render.bFreezeGIField);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Stops probe trace/blend/relocation and radiance-cache shading; carry-forward suspends eviction and pins cell ages so unfreezing does not mass-evict the table.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Screen Feedback##freeze", &state->debug.render.bFreezeScreenFeedback);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Disables the gather screen tier and reflection screen-space hit lighting. Lit history is view-dependent and keeps evolving, which breaks frozen determinism and face-seams probe bakes.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Gather Ray##freeze", &state->debug.render.bFreezeGatherRay);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Forces the gather onto its skip-ray path (radiance cache at the pixel surface, probes as fallback) while frozen, removing the 1spp cosine ray entirely.");
    }
}

void DrawLightingWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (ImGui::Begin("Lighting")) {
        bool changed = false;

        auto featureSection = [&](const char* label, bool* enabled, auto&& body) {
            ImGui::PushID(label);
            if (ImGui::Checkbox("##enabled", enabled)) { changed = true; }
            ImGui::SameLine();
            const bool open = ImGui::TreeNodeEx("##section", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", label);
            if (open) {
                ImGui::BeginDisabled(!*enabled);
                body();
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            ImGui::PopID();
        };

        if (ImGui::CollapsingHeader("Probe Bake")) {
            DrawProbeBakeSection(ctx, state);
        }

        ImGui::Separator();

        if (Widgets::SaveBar("lighting", &state->projectConfig.bAutoSaveLighting)) {
            SaveLightingTab(state);
        }
        DrawLightingProfiles(state);

        ImGui::Separator();

        const bool bIsGroundTruth = state->lighting.groundTruthMode != Core::GroundTruthMode::None;
        ImGui::BeginDisabled(bIsGroundTruth);
        // Shading Pipeline Overrides
        {
            Core::Span<const StringID> shadingPipelines = ctx->pipelineManager->GetShadingPipelines();
            const int32_t pipelineCount = static_cast<int32_t>(shadingPipelines.Size());
            Core::Arena& arena = ctx->editorArena.Get();

            int currentShader = pipelineCount; // "None"
            for (int32_t i = 0; i < pipelineCount; ++i) {
                if (state->debug.shadingShaderOverride == shadingPipelines[i]) {
                    currentShader = i;
                    break;
                }
            }

            Core::ArenaArray<Core::InlineString<> > labels(&arena, pipelineCount + 1);
            labels[0] = Core::InlineString("None");
            for (int32_t i = 0; i < pipelineCount; ++i) { labels[i + 1] = Core::InlineString(shadingPipelines[i].ToString()); }
            const int comboIndex = currentShader == pipelineCount ? 0 : currentShader + 1;
            int selected = comboIndex;
            auto getter = [](void* data, int idx) -> const char* { return (*static_cast<Core::ArenaArray<Core::InlineString<> >*>(data))[idx].c_str(); };
            if (ImGui::Combo("Shading Override", &selected, getter, &labels, static_cast<int32_t>(labels.Size()))) {
                state->debug.shadingShaderOverride = selected == 0 ? StringID{} : shadingPipelines[selected - 1];
            }
        }
        // Lighting Pipeline Overrides
        {
            Core::Span<const StringID> lightingPipelines = ctx->pipelineManager->GetLightingPipelines();
            const int32_t pipelineCount = static_cast<int32_t>(lightingPipelines.Size());
            Core::Arena& arena = ctx->editorArena.Get();

            int currentShader = pipelineCount; // "None"
            for (int32_t i = 0; i < pipelineCount; ++i) {
                if (state->debug.lightingShaderOverride == lightingPipelines[i]) {
                    currentShader = i;
                    break;
                }
            }

            Core::ArenaArray<Core::InlineString<> > labels(&arena, pipelineCount + 1);
            labels[0] = Core::InlineString("None");
            for (int32_t i = 0; i < pipelineCount; ++i) { labels[i + 1] = Core::InlineString(lightingPipelines[i].ToString()); }
            const int comboIndex = currentShader == pipelineCount ? 0 : currentShader + 1;
            int selected = comboIndex;
            auto getter = [](void* data, int idx) -> const char* { return (*static_cast<Core::ArenaArray<Core::InlineString<> >*>(data))[idx].c_str(); };
            if (ImGui::Combo("Lighting Override", &selected, getter, &labels, static_cast<int32_t>(labels.Size()))) {
                state->debug.lightingShaderOverride = selected == 0 ? StringID{} : lightingPipelines[selected - 1];
            }
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        const char* lightingModeLabels[] = {"Default", "ReSTIR", "Path Tracing"};
        int32_t lightingModeIndex = static_cast<int32_t>(state->lighting.lightingMode);
        if (ImGui::Combo("Lighting Mode", &lightingModeIndex, lightingModeLabels, IM_ARRAYSIZE(lightingModeLabels))) {
            state->lighting.lightingMode = static_cast<Core::LightingMode>(lightingModeIndex);
            changed = true;
        }

        ImGui::SeparatorText("Ground-Truth Reference"); {
            auto gtToggle = [&](const char* label, Core::GroundTruthMode mode) {
                const bool active = state->lighting.groundTruthMode == mode;
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.75f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.40f, 0.70f, 1.0f));
                }
                if (ImGui::Button(label)) {
                    state->lighting.groundTruthMode = active ? Core::GroundTruthMode::None : mode;
                    state->lighting.bResetGroundTruth = true;
                }
                if (active) { ImGui::PopStyleColor(3); }
            };
            gtToggle("DI", Core::GroundTruthMode::DI);
            ImGui::SameLine();
            gtToggle("GI", Core::GroundTruthMode::GI);
            ImGui::SameLine();
            gtToggle("Full", Core::GroundTruthMode::Full);
            ImGui::SliderInt("Samples/Frame##gt", &state->lighting.groundTruthSpp, 1, 32);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Path-traced samples per pixel per frame; Full mode only, DI/GI stay 1. The probe bake overrides this with its own GT Samples/Frame while baking.");
            }
        }

        ImGui::SeparatorText("Render Cache Reset"); {
            if (ImGui::Button("Full Renderer Clear")) {
                state->requests.pendingCacheReset = Core::RenderCacheReset::All;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Screen History")) {
                state->requests.pendingCacheReset = Core::RenderCacheReset::ScreenHistory;
            }
        }

        ImGui::Separator();

        const bool bDefaultMode = state->lighting.lightingMode == Core::LightingMode::Default;
        const bool bReSTIRMode = state->lighting.lightingMode == Core::LightingMode::ReSTIR;

        ImGui::SeparatorText("Both");

        if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (Widgets::SliderFloat("IBL Intensity##env", &state->lighting.iblIntensity, 0.0f, 2.0f)) {
                changed = true;
            }
            if (Widgets::SliderFloat("Indirect Intensity##env", &state->lighting.indirectIntensity, 0.0f, 1.0f, {.tooltip = "Scales the indirect diffuse term at the final composite only (gather/probe/sky fill). Lower = darker shadows. Does not feed back into probe or cache convergence. Default 1.", .reset = true, .resetTo = 1.0})) {
                changed = true;
            }
        }

        if (ImGui::CollapsingHeader("Ground Truth Ambient Occlusion")) {
            Core::GTAOConfiguration& gtao = state->lighting.gtaoConfig;
            static const Core::GTAOConfiguration gtaoDefaults{};

            if (ImGui::Checkbox("Enable GTAO", &gtao.bEnabled)) { changed = true; }

            auto gtaoF = [&](const char* label, float* v, float def, float mn, float mx, const char* fmt, const char* tip) {
                if (Widgets::SliderFloat(label, v, mn, mx, {.format = fmt, .tooltip = tip, .reset = true, .resetTo = def})) { changed = true; }
            };

            gtaoF("Effect Radius##gtao", &gtao.effectRadius, gtaoDefaults.effectRadius, 0.0f, 5.0f, "%.2f", "World-space radius (meters) the occlusion horizon search covers. Larger = broader, softer AO reaching more distant occluders; smaller = tighter contact darkening. Default 0.5.");
            gtaoF("Radius Multiplier##gtao", &gtao.radiusMultiplier, gtaoDefaults.radiusMultiplier, 0.0f, 3.0f, "%.3f", "Scales the sampling radius relative to Effect Radius; the XeGTAO tuning constant that trades screen-space reach against sample density. Default 1.457.");
            gtaoF("Falloff Range##gtao", &gtao.effectFalloffRange, gtaoDefaults.effectFalloffRange, 0.0f, 1.0f, "%.3f", "Fraction of the radius over which occlusion attenuates to zero at the edge. Higher = smoother distance falloff; lower = harder cutoff. Default 0.615.");
            gtaoF("Sample Distribution Power##gtao", &gtao.sampleDistributionPower, gtaoDefaults.sampleDistributionPower, 1.0f, 3.0f, "%.2f", "Bias of step placement along each slice toward the pixel. >1 concentrates samples near the center for stronger contact AO. Default 2.0.");
            gtaoF("Thin Occluder Compensation##gtao", &gtao.thinOccluderCompensation, gtaoDefaults.thinOccluderCompensation, 0.0f, 0.7f, "%.2f", "Reduces over-darkening behind thin geometry (railings, foliage) by assuming occluders have limited thickness. Keep <= 0.7. Default 0.0.");
            gtaoF("Final Value Power##gtao", &gtao.finalValuePower, gtaoDefaults.finalValuePower, 0.5f, 5.0f, "%.2f", "Exponent applied to the final AO term. Higher = darker, higher-contrast occlusion; lower = subtler. Default 2.2.");
            gtaoF("Depth MIP Sampling Offset##gtao", &gtao.depthMipSamplingOffset, gtaoDefaults.depthMipSamplingOffset, 0.0f, 5.0f, "%.2f", "Controls how aggressively coarser depth mips are used for far samples. Higher = cheaper/blurrier distant AO; lower = sharper but costlier. Default 3.3.");
            gtaoF("Slice Count##gtao", &gtao.sliceCount, gtaoDefaults.sliceCount, 1.0f, 9.0f, "%.0f", "Number of angular slices sampled per pixel. More = smoother, less directional noise, higher cost. Default 5.");
            gtaoF("Steps Per Slice##gtao", &gtao.stepsPerSlice, gtaoDefaults.stepsPerSlice, 1.0f, 9.0f, "%.0f", "Horizon-march steps taken along each slice. More = finer occluder detection, higher cost. Default 3.");
            gtaoF("Denoise Blur Beta##gtao", &gtao.denoiseBlurBeta, gtaoDefaults.denoiseBlurBeta, 0.0f, 10.0f, "%.2f", "Edge-stopping strength of the final denoise blur. Higher = preserves edges but leaves more noise; lower = smoother but softer. Default 1.2.");
            gtaoF("Denoise Passes##gtao", &gtao.denoisePasses, gtaoDefaults.denoisePasses, 1.0f, 8.0f, "%.0f", "Edge-aware denoise passes over the raw AO. Each is a full-res dispatch; intermediate passes blur harder than the last. The XeGTAO default of 1 assumes TAA finishes the job. Default 2.");

            ImGui::Spacing();
            if (ImGui::Button("Reset GTAO")) {
                gtao = Core::GTAOConfiguration{};
                changed = true;
            }
        }

        if (ImGui::CollapsingHeader("Reflections")) {
            Core::ReflectionConfiguration& reflection = state->lighting.reflection;
            static const Core::ReflectionConfiguration reflectionDefaults{};

            if (ImGui::Checkbox("Enable Reflections", &reflection.bEnabled)) { changed = true; }
            if (ImGui::Checkbox("Merged Denoise", &reflection.bMergedDenoise)) { changed = true; }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("ReSTIR mode only: sum the traced reflection radiance into the main denoiser's specular channel at the lighting resolve, so one denoiser covers lights + sun + reflections. Off: the raw noisy shade output composites directly in remodulate."); }
            if (ImGui::Checkbox("Screen-Space Hit Lighting", &reflection.bScreenSpaceLighting)) { changed = true; }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Reproject the reflection hit into last frame's lit image and reuse that fully shadowed color; falls back to unshadowed analytic hit shading when the hit is off-screen or occluded."); }
            if (ImGui::Checkbox("Screen-Space Trace", &reflection.bScreenSpaceTrace)) { changed = true; }

            static const char* reflectionSunModes[] = {"Shadow Ray", "Always Lit", "Always Unlit"};
            int reflectionSunMode = static_cast<int>(reflection.sunMode);
            if (ImGui::Combo("Hit Sun Mode##reflection", &reflectionSunMode, reflectionSunModes, IM_ARRAYSIZE(reflectionSunModes))) {
                reflection.sunMode = static_cast<Core::ReflectionConfiguration::SunMode>(reflectionSunMode);
                changed = true;
            }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Sun term when a reflection hit falls back to analytic shading (screen-space reuse missed). Shadow Ray traces sun visibility at the hit; Always Lit skips the ray and assumes visible; Always Unlit drops the sun entirely (indoor scenes)."); }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Default mode only: march the reflection ray against the depth buffer instead of the TLAS. Off-screen and occluded rays fall back to reflection probes then the skybox."); }

            auto reflF = [&](const char* label, float* v, float def, float mn, float mx, const char* fmt, const char* tip) {
                if (Widgets::SliderFloat(label, v, mn, mx, {.format = fmt, .tooltip = tip, .reset = true, .resetTo = def})) { changed = true; }
            };

            reflF("Traced Roughness Max##reflection", &reflection.tracedRoughnessMax, reflectionDefaults.tracedRoughnessMax, 0.0f, 1.0f, "%.2f", "Surfaces rougher than this fall back to the prefiltered skybox reflection instead of being ray traced. Lower = only near-mirror surfaces get traced reflections, cheaper. Default 0.3.");
            reflF("Light Specular From Reflections Max##reflection", &reflection.lightSpecularFromReflectionsMax, reflectionDefaults.lightSpecularFromReflectionsMax, 0.0f, 1.0f, "%.2f",
                  "Roughness at/below which local-light specular is left to the reflection providers (probes/RT) instead of shaded analytically. 1.0 = providers own all specular; low = only near-mirror deferred. Default 0.3 (= traced max; DI owns rough spec, probe bakes hide light proxies to avoid double count).");
            reflF("Intensity##reflection", &reflection.intensity, reflectionDefaults.intensity, 0.0f, 2.0f, "%.2f", "Multiplier on the traced reflection radiance before compositing. Default 1.0.");
            reflF("SSR Thickness##reflection", &reflection.ssrThickness, reflectionDefaults.ssrThickness, 0.05f, 2.0f, "%.2f", "Screen-space trace only: view-space depth window (meters) behind a surface that still counts as a hit. Larger = fewer gaps but more over-reflection behind thin objects. Default 0.3.");
            if (Widgets::SliderInt("SSR Max Steps##reflection", &reflection.ssrMaxSteps, 16, 256, {.tooltip = "Screen-space trace only: maximum march steps per ray before giving up. Higher = longer reflections, higher cost. Default 64.", .reset = true, .resetTo = static_cast<double>(reflectionDefaults.ssrMaxSteps)})) { changed = true; }


            ImGui::Spacing();
            if (ImGui::Button("Reset RT Reflections")) {
                reflection = Core::ReflectionConfiguration{};
                changed = true;
            }
        }

        if (ImGui::CollapsingHeader("Reflection Probes")) {
            Core::ReflectionProbeConfiguration& reflectionProbe = state->lighting.reflectionProbe;

            if (ImGui::Checkbox("Enable Reflection Probes", &reflectionProbe.bEnabled)) { changed = true; }
            if (Widgets::SliderFloat("Probe Intensity##reflectionprobe", &reflectionProbe.intensity, 0.0f, 2.0f, {.format = "%.2f", .reset = true, .resetTo = 1.0})) { changed = true; }
            if (ImGui::Checkbox("Debug Draw Probe Volumes", &reflectionProbe.bDebugDraw)) { changed = true; }
            if (Widgets::SliderFloat("Baked Diffuse Clamp K##reflectionprobe", &reflectionProbe.bakedDiffuseClampK, 1.0f, 16.0f, {.format = "%.1f", .tooltip = "Luminance-ratio ceiling for the radiance-cache diffuse tier inside a probe volume: cache is scaled down when it exceeds K times the baked probe irradiance. Default 4.0.", .reset = true, .resetTo = 4.0})) { changed = true; }
            if (ImGui::Checkbox("Brute-Force Probe Pick", &reflectionProbe.bBruteForcePick)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Debug: bypass the world-grid probe bin and scan all probes per pixel.");
            }

            ImGui::Spacing();
            if (ImGui::Button("Reset Reflection Probes")) {
                reflectionProbe = Core::ReflectionProbeConfiguration{};
                changed = true;
            }
        }

        if (ImGui::CollapsingHeader("DDGI")) {
            Core::DDGIParams& ddgi = state->lighting.ddgi;
            static const Core::DDGIParams ddgiDefaults{};

            auto ddgiF = [&](const char* label, float* v, float def, float mn, float mx, const char* fmt, const char* tip) {
                if (Widgets::SliderFloat(label, v, mn, mx, {.format = fmt, .tooltip = tip, .reset = true, .resetTo = def})) { changed = true; }
            };
            auto ddgiI = [&](const char* label, int* v, int def, int mn, int mx, const char* tip) {
                if (Widgets::SliderInt(label, v, mn, mx, {.tooltip = tip, .reset = true, .resetTo = static_cast<double>(def)})) { changed = true; }
            };

            if (ImGui::Checkbox("Enabled##ddgi", &ddgi.bEnabled)) { changed = true; }
            ImGui::SameLine();
            if (ImGui::Checkbox("Apply To Lighting##ddgi", &ddgi.bApplyToLighting)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Use the probes as the indirect diffuse in lighting (replaces the skybox irradiance where the volume covers). Off = probes still update, for A/B and the debug viz.");
            }
            if (ImGui::Checkbox("GI Diffuse Gather##ddgi", &ddgi.bFinalGather)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("TDA-style resolve: one cosine ray per half-res pixel reads last frame's screen at its hit, then the radiance cache (probes as fallback, skybox on miss) instead of sampling probes at the pixel. Debug views live in the Debug View window.");
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Denoise##gigather", &ddgi.bFinalGatherDenoise)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Separable bilateral blur on the gather SH before compositing (normal/depth/hit-distance edge stopping). Off = raw 1spp signal, for A/B.");
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Chroma##gigather", &ddgi.bFinalGatherChromaDenoise)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Extra denoise iterations on chroma (CoCg chromaticity) only, luminance carried. Targets the low-frequency red/blue patching without softening luminance detail. Requires Denoise.");
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Temporal##gigather", &ddgi.bFinalGatherTemporal)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Counter accumulation of the resolved gather across frames (up to 32). Off = this frame's result only; with Denoise also off the composite shows the raw gather.");
            }
            int gatherRaysPerPixel = static_cast<int>(ddgi.gatherRaysPerPixel);
            if (Widgets::SliderInt("Rays Per Pixel##gigather", &gatherRaysPerPixel, 1, static_cast<int>(Render::GI_GATHER_MAX_RAYS_PER_PIXEL), {
                                       .tooltip = "Gather rays per half-res pixel, uniform across the frame so cost stays flat and rays stay coherent. Relative noise falls as 1/sqrt(n), which is the only lever that reaches the bright-to-dark gradients near small light slits, where one ray finds the aperture too rarely for any reweighting to help. Trace cost is linear.", .reset = true,
                                       .resetTo = 1.0
                                   })) {
                ddgi.gatherRaysPerPixel = static_cast<uint32_t>(gatherRaysPerPixel);
                changed = true;
            }
            int gatherChromaPasses = static_cast<int>(ddgi.gatherChromaDenoisePasses);
            if (Widgets::SliderInt("Chroma Passes##gigather", &gatherChromaPasses, 1, 4, {.tooltip = "Chroma-only denoise pass count; strides 8/16/32/64, each added pass doubles the hue-smoothing reach. Default 2.", .reset = true, .resetTo = 2.0})) {
                ddgi.gatherChromaDenoisePasses = static_cast<uint32_t>(gatherChromaPasses);
                changed = true;
            }
            if (Widgets::SliderFloat("Chroma Luma Ratio Power##gigather", &ddgi.gatherChromaLumaPower, 0.0f, 6.0f, {
                                         .tooltip = "Falloff on the tap/center luminance ratio. Chroma taps carry no luminance and every other weight in these passes is geometric, so without this a cast shadow (same plane, same normal, same AO) takes the lit side's hue as a colored halo. 0 = off (pre-2026-07-30 behaviour). Integer values compile to a multiply chain. Default 2.", .reset = true,
                                         .resetTo = 2.0
                                     })) {
                changed = true;
            }
            if (ImGui::Checkbox("Skip Ray (Cache + Probes Only)##gigather", &ddgi.bGatherSkipRay)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Skips the per-pixel cosine ray entirely: samples the radiance cache at the pixel's own surface point (probes as fallback, skybox on miss) instead of tracing. No ray noise, but resolution is capped by the cache's cell size (blockier); still runs through the same denoise/upscale/temporal pipeline. Best for clean/simply-textured scenes where the ray's 1spp noise isn't worth it.");
            }

            ImGui::SeparatorText("Volume");
            ddgiI("Probe Count X##ddgi", &ddgi.probeCountX, ddgiDefaults.probeCountX, 2, 32, "Probes along X. The volume is a camera-following rolling window; changing counts restarts probe history.");
            ddgiI("Probe Count Y##ddgi", &ddgi.probeCountY, ddgiDefaults.probeCountY, 2, 32, "Probes along Y (vertical).");
            ddgiI("Probe Count Z##ddgi", &ddgi.probeCountZ, ddgiDefaults.probeCountZ, 2, 32, "Probes along Z.");
            ddgiF("Probe Spacing##ddgi", &ddgi.probeSpacing, ddgiDefaults.probeSpacing, 0.25f, 8.0f, "%.2f", "World-space distance between probes (meters); coverage = count * spacing per axis. Changing it restarts probe history.");
            int cascadeCount = static_cast<int>(ddgi.cascadeCount);
            if (Widgets::SliderInt("Cascade Count##ddgi", &cascadeCount, 1, static_cast<int>(DDGI_MAX_CAMERA_CASCADES), {
                                       .tooltip = "Concentric volumes with identical counts; each doubles the previous spacing, so range doubles per cascade for linear memory. Cascade 0 updates every frame, outer cascades round-robin one per frame (flat trace cost).", .reset = true, .resetTo = static_cast<double>(ddgiDefaults.cascadeCount)
                                   })) {
                ddgi.cascadeCount = static_cast<uint32_t>(cascadeCount);
                changed = true;
            }
            ddgiF("Edge Blend Cells##ddgi", &ddgi.edgeBlendCells, ddgiDefaults.edgeBlendCells, 1.0f, 8.0f, "%.1f", "Width (in probe cells) of each cascade's edge fade into the next coarser cascade (and the outermost cascade's fade to skybox). Wider = softer, less visible cascade boundary; costs double-sampling in the band.");
            if (ImGui::Checkbox("Scale Biases Per Cascade##ddgi", &ddgi.bScaleBiasPerCascade)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Multiply normal/view bias by each cascade's spacing scale (RTXGI-style). Off = all cascades sample at the same world-space bias, which can reduce fine-vs-coarse disagreement at cascade boundaries.");
            }
            if (ImGui::Checkbox("Local Volumes##ddgi", &ddgi.bLocalVolumes)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Hand-placed fine-spacing probe volumes (LocalDDGIVolumeComponent entities), sampled before cascade 0 where they cover. Nearest few volumes stay resident, one updates per frame.");
            }
            int maxResidentWorldVolumes = static_cast<int>(ddgi.maxResidentWorldVolumes);
            if (Widgets::SliderInt("Max Resident Volumes##ddgi", &maxResidentWorldVolumes, 1, static_cast<int>(DDGI_MAX_RESIDENT_LOCAL_VOLUMES), {
                                       .tooltip = "Hand-placed world volumes kept resident (nearest first). The shared probe atlas is allocated in buckets of 10 rows, so it shrinks with this and every volume reconverges whenever the bucket changes.", .reset = true, .resetTo = static_cast<double>(ddgiDefaults.maxResidentWorldVolumes)
                                   })) {
                ddgi.maxResidentWorldVolumes = maxResidentWorldVolumes;
                changed = true;
            }
            int worldVolumeWarmupBoost = static_cast<int>(ddgi.worldVolumeWarmupBoost);
            if (Widgets::SliderInt("Warmup Boost##ddgi", &worldVolumeWarmupBoost, 1, 32, {
                                       .tooltip = "World volumes updated per frame while any resident one is still cold, instead of the steady-state one. At a high resident count a single update per frame never converges a volume that just came into range; the budget collapses back to 1 once everything has warmed.", .reset = true, .resetTo = static_cast<double>(ddgiDefaults.worldVolumeWarmupBoost)
                                   })) {
                ddgi.worldVolumeWarmupBoost = worldVolumeWarmupBoost;
                changed = true;
            }
            if (ImGui::Checkbox("Cascade Sampling##ddgi", &ddgi.bCascadeSampling)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Debug: off = consumers (composite, gather, cache shade, bounce feedback) sample local volumes only; cascade windows keep updating and still suppress sky via edge fade, so toggling back is instant.");
            }
            if (ImGui::Checkbox("World Volume Grid Cull##ddgi", &ddgi.bWorldVolumeGridCull)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Debug: off = the sampler walks every resident world volume instead of the world grid's per-cell overlap list. Same result, slower; a difference means the bin is dropping volumes.");
            }

            ImGui::SeparatorText("Trace");
            int raysPerProbe = static_cast<int>(ddgi.raysPerProbe);
            if (Widgets::SliderInt("Rays Per Probe##ddgi", &raysPerProbe, 16, 256, {.tooltip = "Rays traced per probe per frame. More rays = less temporal noise per frame, linearly more trace cost.", .reset = true, .resetTo = 128.0})) {
                ddgi.raysPerProbe = static_cast<uint32_t>(raysPerProbe);
                changed = true;
            }
            int outerRaysPerProbe = static_cast<int>(ddgi.outerRaysPerProbe);
            if (Widgets::SliderInt("Outer Rays Per Probe##ddgi", &outerRaysPerProbe, 16, 256, {.tooltip = "Rays per probe for cascades past the first. Outer probes hold 2-8x coarser detail sampled through wide blending, so they tolerate fewer rays; costs slightly noisier visibility and slower relocation there.", .reset = true, .resetTo = 64.0})) {
                ddgi.outerRaysPerProbe = static_cast<uint32_t>(outerRaysPerProbe);
                changed = true;
            }
            if (ImGui::Checkbox("Probe Classification##ddgi", &ddgi.bClassification)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Probes with no geometry within ~1.5 cells drop to 16 sentinel rays and freeze their atlas tiles (nothing within sampling reach reads them); a sentinel hit reactivates the probe with a temporal restart. Requires Relocation (classification rides that pass). Inactive probes draw flat blue in the probe debug view.");
            }
            if (ImGui::Checkbox("Infinite Bounce##ddgi", &ddgi.bInfiniteBounce)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Ray hits also sample last frame's probe atlas, so light keeps bouncing (one extra bounce lands per frame, damped by hysteresis). Also gives area/sphere lights indirect, since probes see their proxies directly.");
            }
            ddgiF("Bounce Intensity##ddgi", &ddgi.bounceIntensity, ddgiDefaults.bounceIntensity, 0.0f, 1.0f, "%.2f",
                  "Scales the DDGI feedback term fed back into the radiance cache / probes. This is the cache<->DDGI feedback loop, so <1 bounds the loop gain: keeps enclosed high-albedo scenes from saturating and self-lighting. 1 = physically full multi-bounce (can run away in red/boxed geometry). Default 0.75.");
            ddgiF("Max Ray Radiance##ddgi", &ddgi.maxRayRadiance, ddgiDefaults.maxRayRadiance, 0.0f, 100.0f, "%.1f", "Firefly clamp: hit radiance above this (max channel) is scaled down before blending, taming NEE light-selection spikes and rare bright emissive hits. Dims indirect from very bright small sources. 0 = off. Default 20.");

            ImGui::SeparatorText("Blend");
            if (ImGui::Button("Converge Now##ddgi")) {
                state->debug.bGIFreeze = false;
                DDGIConvergeBoostTrigger(state->ddgiConvergeBoost, ddgi);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Temporarily drops hysteresis, maxes rays, and accelerates radiance-cache shading (interval + accumulation window) for ~70 frames so dark multi-bounce rooms converge quickly, then restores the values below. Retrigger to restart the schedule.");
            }
            if (state->ddgiConvergeBoost.bActive) {
                ImGui::SameLine();
                ImGui::Text("Converging... %d frames left", DDGI_CONVERGE_BOOST_FRAMES - state->ddgiConvergeBoost.frame);
            }
            ddgiF("Hysteresis##ddgi", &ddgi.hysteresis, ddgiDefaults.hysteresis, 0.0f, 0.995f, "%.3f", "Temporal history weight for irradiance (RTXGI parity). Probe updates are 1spp Monte Carlo, so the EMA carries most of the smoothing; the darkening fast path plus the min darkening step keep lights-off response quick. Higher = smoother but laggier; 0 = no history. Default 0.97.");
            ddgiF("Visibility Hysteresis##ddgi", &ddgi.visibilityHysteresis, ddgiDefaults.visibilityHysteresis, 0.0f, 0.995f, "%.3f", "Temporal history weight for the distance/Chebyshev atlas. The radiance cache stabilizes radiance only; the visibility integrand still changes every frame with ray rotation, so this stays high. Default 0.97.");
            int cacheShadeInterval = static_cast<int>(ddgi.radianceCacheShadeInterval);
            if (Widgets::SliderInt("Cache Shade Interval##ddgi", &cacheShadeInterval, 1, 32, {
                                       .tooltip = "Frames between radiance-cache cell re-shades (plus a 0-3 per-slot stagger). Lower = the cache tracks lighting changes faster, at more shade dispatch cost. Interbounce light propagates one cache shade + one probe blend per generation, so this bounds multi-bounce convergence speed. Default 8.", .reset = true,
                                       .resetTo = static_cast<double>(ddgiDefaults.radianceCacheShadeInterval)
                                   })) {
                ddgi.radianceCacheShadeInterval = static_cast<uint32_t>(cacheShadeInterval);
                changed = true;
            }
            int cacheAccumCap = static_cast<int>(ddgi.radianceCacheAccumCap);
            if (Widgets::SliderInt("Cache Accum Frames##ddgi", &cacheAccumCap, 1, 64, {
                                       .tooltip = "Running-mean window cap for cache cell radiance: each shade event blends 1/(count+1) up to this. Lower = faster response, more variance; the change-streak dump already cuts history on sustained changes. Default 16.", .reset = true, .resetTo = static_cast<double>(ddgiDefaults.radianceCacheAccumCap)
                                   })) {
                ddgi.radianceCacheAccumCap = static_cast<uint32_t>(cacheAccumCap);
                changed = true;
            }
            ddgiF("Irradiance Gamma##ddgi", &ddgi.irradianceGamma, ddgiDefaults.irradianceGamma, 1.0f, 10.0f, "%.1f", "Perceptual encoding exponent: the atlas stores pow(E, 1/gamma) and blends in that space, so rare bright rays (sky through a small opening) cannot pulse the average. 1 = linear. Default 5.");
            ddgiF("Irradiance Threshold##ddgi", &ddgi.irradianceThreshold, ddgiDefaults.irradianceThreshold, 0.0f, 1.0f, "%.2f", "Encoded-space darkening that counts as a real lighting change (lights turning off, the low-variance direction; RTXGI): hysteresis drops by 0.75 so the probe re-converges fast. Brightening spikes stay damped by hysteresis + the delta clamp. Default 0.25.");
            ddgiF("Brightness Threshold##ddgi", &ddgi.brightnessThreshold, ddgiDefaults.brightnessThreshold, 0.0f, 1.0f, "%.2f", "Encoded-space per-frame change clamp: deltas above this are scaled to 25% (firefly/pulse suppression). Default 0.10.");
            ddgiF("Distance Exponent##ddgi", &ddgi.distanceExponent, ddgiDefaults.distanceExponent, 1.0f, 100.0f, "%.0f", "Sharpness of the cosine lobe used when integrating ray distances into the visibility atlas; higher = tighter Chebyshev occlusion, more leak-proof but noisier. Default 50.");

            ImGui::SeparatorText("Sampling");
            ddgiF("Normal Bias##ddgi", &ddgi.normalBias, ddgiDefaults.normalBias, 0.0f, 1.0f, "%.2f", "Meters the sample point is pushed along the surface normal before probe lookup; fights self-shadowing (dark stripes on walls). Default 0.10.");
            ddgiF("View Bias##ddgi", &ddgi.viewBias, ddgiDefaults.viewBias, 0.0f, 2.0f, "%.2f", "Meters the sample point is pushed toward the viewer before probe lookup; fights leaks through thin walls near the camera ray. Default 0.30.");

            ImGui::SeparatorText("Relocation");
            if (ImGui::Checkbox("Relocation##ddgi", &ddgi.bRelocation)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Probes inside geometry step out through the nearest backface (capped at 45% of spacing); probes still buried past the cap are classified dead and skipped at sampling (drawn red in the probe debug view).");
            }
            ddgiF("Min Frontface Distance##ddgi", &ddgi.minFrontfaceDistance, ddgiDefaults.minFrontfaceDistance, 0.0f, 1.0f, "%.2f", "Meters of clearance relocation keeps between a probe and nearby geometry; probes closer than this to a wall get nudged away from it. Default 0.30.");

            ImGui::SeparatorText("Radiance Cache Occupancy"); {
                // Read-only; multi-frame readback latency, so values trail the live cache by 2-3 frames.
                const Engine::RadianceCacheStatsSnapshot& wc = ctx->radianceCacheStats;
                const float occupancyPct = 100.0f * static_cast<float>(wc.occupiedSlots) / static_cast<float>(RADIANCE_CACHE_HASH_CAPACITY);
                ImGui::Text("Occupancy: %.1f%% (%u / %u)", occupancyPct, wc.occupiedSlots, RADIANCE_CACHE_HASH_CAPACITY);
                ImGui::Text("Shades/frame: %u", wc.cellsShaded);
                ImGui::Text("Evictions/frame: %u", wc.cellsEvicted);
                ImGui::Text("Inserts failed/frame: %u", wc.insertsFailed);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Full-probe hash insert failures (trace + carry-forward combined). Non-zero means the cache is over capacity and cells are being dropped.");
                }
                const float shadeDenom = static_cast<float>(glm::max(wc.cellsShaded, 1u));
                ImGui::Text("Streak dumps/frame: %u (%.1f%%)", wc.cellsDumped, 100.0f * static_cast<float>(wc.cellsDumped) / shadeDenom);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Shade events where the change-streak detector fired and cut accumulated history. A high percentage means the detector is reading representative variance as real change and the cache is not accumulating.");
                }
                ImGui::Text("Dark cells/frame: %u (%.1f%%)", wc.cellsDark, 100.0f * static_cast<float>(wc.cellsDark) / shadeDenom);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Shade events whose previous luma sat below 0.01, where the relative change threshold degenerates into a fixed absolute of 0.0035 and trips on ordinary noise.");
                }
            }

            ImGui::Spacing();
            if (ImGui::Button("Reset DDGI")) {
                ddgi = Core::DDGIParams{};
                changed = true;
            }
        }

        if (bDefaultMode || bReSTIRMode) {
            ImGui::SeparatorText("Directional Sun Shadow");

            if (ImGui::CollapsingHeader("SIGMA Shadow Denoiser")) {
                Core::SIGMAParams& sigma = state->lighting.sigmaParams;
                static const Core::SIGMAParams sigmaDefaults{};

                auto sigmaTip = [&](const char* tip) {
                    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("%s", tip); }
                };

                if (ImGui::Checkbox("Half Res##sigma", &sigma.bHalfRes)) { changed = true; }
                sigmaTip("Trace + denoise the sun shadow at half resolution, then bilaterally upsample. Cuts the trace/temporal cost; softens contact shadows. Matches half-res ReSTIR.");
                if (ImGui::Checkbox("Post-Blur##sigma", &sigma.enablePostBlur)) { changed = true; }
                sigmaTip("Second decorrelated spatial pass after the main blur. The single largest quality lever; cleans residual penumbra noise. Default on.");

                auto sigmaF = [&](const char* label, float* v, float def, float mn, float mx, const char* fmt, const char* tip) {
                    if (Widgets::SliderFloat(label, v, mn, mx, {.format = fmt, .tooltip = tip, .reset = true, .resetTo = def})) { changed = true; }
                };
                sigmaF("History Weight##sigma", &sigma.historyWeight, sigmaDefaults.historyWeight, 0.0f, 0.875f, "%.2f", "Temporal stabilization strength. Higher = steadier but laggier on moving shadows; lower = snappier but shimmerier. Saturates at 0.875 (SIGMA history cap). Default 0.8.");
                sigmaF("Max Kernel Pixels##sigma", &sigma.maxKernelPixels, sigmaDefaults.maxKernelPixels, 1.0f, 64.0f, "%.0f", "Cap on the penumbra blur radius (px). Bounds cost on very soft shadows. Default 32.");
                sigmaF("Penumbra Scale##sigma", &sigma.penumbraScale, sigmaDefaults.penumbraScale, 0.0f, 4.0f, "%.2f", "Artistic multiplier on the estimated penumbra. >1 softer, <1 sharper. Default 1.0.");

                if (ImGui::Button("Reset SIGMA")) {
                    sigma = Core::SIGMAParams{};
                    changed = true;
                }
            }
        }

        if (bReSTIRMode) {
            ImGui::SeparatorText("ReSTIR Rendering");

            if (ImGui::CollapsingHeader("ReSTIR Options", ImGuiTreeNodeFlags_DefaultOpen)) {
                Core::ReSTIRParams& restir = state->debug.restir;
                const bool bReGIR = state->lighting.lightingMode == Core::LightingMode::ReSTIR;

                // Sections compiled out via restir_features_macros.h are greyed: the runtime toggle has no effect until the macro is set to 1 and shaders are rebuilt.
                ImGui::SeparatorText("Base (Candidate Generation)");
                const char* proposalModes[] = {"World Grid Bin", "ReGIR"};
                int proposalIdx = static_cast<int>(restir.lightProposal);
                if (ImGui::Combo("Light Proposal", &proposalIdx, proposalModes, IM_ARRAYSIZE(proposalModes))) {
                    restir.lightProposal = static_cast<Core::ReSTIRParams::LightProposal>(proposalIdx);
                    changed = true;
                }
                if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", "Candidate source for ReSTIR DI. World Grid Bin: cascaded strongest-K analytic bin (sparse analytic scenes). ReGIR: reservoir hash grid (dense/emissive-triangle scenes; schedules the presample/fill producer chain)."); }
                ImGui::BeginDisabled(!RESTIR_ENABLE_INITIAL_VISIBILITY);
                if (ImGui::Checkbox("Initial Candidate Visibility", &restir.bInitialVisibility)) {
                    changed = true;
                }
                ImGui::EndDisabled();

                ImGui::SeparatorText("Temporal");
                if (ImGui::Checkbox("Temporal Reuse", &restir.bEnableTemporal)) {
                    changed = true;
                }
                int temporalMCap = static_cast<int>(restir.temporalMCap);
                if (Widgets::SliderInt("Temporal M Cap", &temporalMCap, 1, 2000)) {
                    restir.temporalMCap = static_cast<uint32_t>(temporalMCap);
                    changed = true;
                }
                if (ImGui::Checkbox("Checkerboard Rendering", &restir.bCheckerboard)) {
                    changed = true;
                }
                ImGui::BeginDisabled(!restir.bCheckerboard);
                if (ImGui::Checkbox("Full-Rate Resolve", &restir.bCheckerboardFullRateResolve)) {
                    changed = true;
                }
                if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", "Keeps the expensive local-light reservoir passes half-rate, but traces sun visibility full-rate and shades every pixel. Hole pixels borrow a depth-matched horizontal neighbor's local reservoir and re-shade it at their own surface. The denoisers then receive a full-rate signal with no checkerboard reconstruction."); }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!RESTIR_ENABLE_PERMUTATION_SAMPLING);
                if (ImGui::Checkbox("Permutation Sampling", &restir.bPermutationSampling)) {
                    changed = true;
                }
                ImGui::EndDisabled();
                if (Widgets::SliderFloat("Boiling Filter (0=off)", &restir.boilingFilterStrength, 0.0f, 1.0f)) {
                    changed = true;
                }
                ImGui::SeparatorText("Sun");
                if (ImGui::Checkbox("Sun Visibility Pass", &restir.bSunLight)) {
                    changed = true;
                }
                if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", "Directional sun as a 1spp cone-traced visibility pass, denoised by RELAX with the local lights. Off: SIGMA + directional composite path."); }

                ImGui::BeginDisabled(!RESTIR_ENABLE_ANTILAG);
                featureSection("Antilag", &restir.bEnableAntilag, [&] {
                    if (Widgets::SliderFloat("Antilag Strength##restir", &restir.antilagStrength, 0.0f, 1.0f,
                                             {.format = "%.2f", .tooltip = "Shrinks carried temporal M where the shadow term flipped vs reprojected history, so moving shadows lose their ghost trail. May add noise in soft-shadow boundaries.", .reset = true, .resetTo = 0.5f})) {
                        changed = true;
                    }
                });
                ImGui::EndDisabled();

                ImGui::SeparatorText("Spatial Reuse");
                int spatialPasses = static_cast<int>(restir.spatialPasses);
                if (Widgets::SliderInt("Spatial Passes (0=off)", &spatialPasses, 0, 8)) {
                    restir.spatialPasses = static_cast<uint32_t>(spatialPasses);
                    changed = true;
                }
                if (restir.spatialPasses > 0u) {
                    int spatialRadius = static_cast<int>(restir.spatialRadius);
                    if (Widgets::SliderInt("Spatial Radius", &spatialRadius, 1, 100)) {
                        restir.spatialRadius = static_cast<uint32_t>(spatialRadius);
                        changed = true;
                    }
                    int spatialNeighbors = static_cast<int>(restir.spatialNeighbors);
                    if (Widgets::SliderInt("Spatial Neighbors", &spatialNeighbors, 1, 16)) {
                        restir.spatialNeighbors = static_cast<uint32_t>(spatialNeighbors);
                        changed = true;
                    }
                    int spatialMCap = static_cast<int>(restir.spatialMCap);
                    if (Widgets::SliderInt("Spatial M Cap", &spatialMCap, 1, 2000)) {
                        restir.spatialMCap = static_cast<uint32_t>(spatialMCap);
                        changed = true;
                    }
                    if (Widgets::SliderFloat("ReSTIR W Clamp (0=off)", &restir.restirWClamp, 0.0f, 100.0f)) {
                        changed = true;
                    }
                    ImGui::BeginDisabled(!RESTIR_ENABLE_SPATIAL_DILATE);
                    featureSection("Spatial Dilate", &restir.bAdaptiveSpatial, [&] {
                        if (Widgets::SliderFloat("Dilate Boost##restir", &restir.adaptiveSpatialBoost, 0.0f, 3.0f)) {
                            changed = true;
                        }
                    });
                    ImGui::EndDisabled();
                }

                ImGui::SeparatorText("Options");
                const char* remodulateOutputModes[] = {"Both", "Diffuse Only", "Specular Only", "Indirect Diffuse (DDGI)"};
                int currentRemodulateOutput = static_cast<int>(restir.remodulateOutput);
                if (ImGui::Combo("Remodulate Output##restir", &currentRemodulateOutput, remodulateOutputModes, IM_ARRAYSIZE(remodulateOutputModes))) {
                    restir.remodulateOutput = static_cast<Core::ReSTIRParams::RemodulateOutput>(currentRemodulateOutput);
                    changed = true;
                }

                featureSection("Emissive Triangle Lights", &restir.bEmissiveTriangleLights, [&] {
                    if (Widgets::SliderFloat("Emissive Range Multiplier", &restir.emissiveTriRangeMultiplier, 0.0f, 64.0f,
                                             {.format = "%.1f", .tooltip = "Attenuation cutoff per triangle: range = multiplier * sqrt(intensity * area). Raise if emissive fixtures darken with distance vs ground truth.", .reset = true, .resetTo = 8.0f})) {
                        changed = true;
                    }
                    if (Widgets::SliderInt("Max Tris Per Emissive Primitive", &restir.emissiveTriMaxPerPrimitive, 1, 16384)) {
                        changed = true;
                    }
                });

                if (bReGIR && restir.lightProposal == Core::ReSTIRParams::LightProposal::ReGIR) {
                    ImGui::SeparatorText("ReGIR");
                    if (Widgets::SliderFloat("ReGIR W Clamp (0=off)", &restir.regirWClamp, 0.0f, 100.0f)) {
                        changed = true;
                    }
                    if (ImGui::Button("Reset ReGIR Grid")) { restir.bResetReGIR = true; }
                }
            }

            if (ImGui::CollapsingHeader("Denoiser", ImGuiTreeNodeFlags_DefaultOpen)) {
                Core::ReSTIRParams& restir = state->debug.restir;

                const char* denoiserModes[] = {"None", "RELAX", "ReBLUR", "NRD RELAX (reference)", "NRD ReBLUR (reference)"};
                constexpr Core::ReSTIRParams::DenoiserMode DENOISER_MODE_ORDER[] = {
                    Core::ReSTIRParams::DenoiserMode::None,
                    Core::ReSTIRParams::DenoiserMode::RELAX,
                    Core::ReSTIRParams::DenoiserMode::ReBLUR,
                    Core::ReSTIRParams::DenoiserMode::NRD,
                    Core::ReSTIRParams::DenoiserMode::NRDReBLUR,
                };
                int denoiserIdx = 0;
                for (int i = 0; i < IM_ARRAYSIZE(denoiserModes); i++) {
                    if (restir.denoiserMode == DENOISER_MODE_ORDER[i]) { denoiserIdx = i; }
                }
                if (ImGui::Combo("Mode##denoiser", &denoiserIdx, denoiserModes, IM_ARRAYSIZE(denoiserModes))) {
                    restir.denoiserMode = DENOISER_MODE_ORDER[denoiserIdx];
                    changed = true;
                }

                if (restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX) {
                    Core::RELAXParams& relax = restir.relax;

                    ImGui::BeginDisabled(!RESTIR_ENABLE_CONFIDENCE);
                    featureSection("Confidence (Moving-Shadow Antilag)", &restir.bEnableConfidence, [&] {
                        if (Widgets::SliderFloat("History Confidence##restir", &state->debug.restir.confidenceStrength, 0.0f, 1.0f,
                                                 {.format = "%.2f", .tooltip = "Moving-shadow antilag: temporal luminance-gradient confidence fed to RELAX (RTXDI-style). Master mix.", .reset = true, .resetTo = 0.75f})) {
                            changed = true;
                        }
                        if (Widgets::SliderFloat("Confidence Sensitivity##restir", &state->debug.restir.confidenceSensitivity, 0.5f, 16.0f,
                                                 {.format = "%.2f", .tooltip = "Pow exponent on the gradient->confidence curve. Higher = collapses history on smaller lighting changes (more aggressive antilag, more noise).", .reset = true, .resetTo = 3.0f})) {
                            changed = true;
                        }
                        if (Widgets::SliderFloat("Confidence Darkness Bias##restir", &state->debug.restir.confidenceDarknessBias, 0.0f, 1.0f,
                                                 {.format = "%.4f", .tooltip = "Floor added to the gradient normalizer so dark-region noise does not produce a large relative gradient (false history collapse).", .reset = true, .resetTo = 0.01f})) {
                            changed = true;
                        }
                        if (Widgets::SliderFloat("Confidence History##restir", &state->debug.restir.confidenceHistoryLength, 0.0f, 16.0f,
                                                 {.format = "%.1f", .tooltip = "Frames the confidence temporal filter holds a dip. Drops fast, recovers slowly; gives ReSTIR time to re-converge before RELAX trusts history again. 0 = no temporal filter.", .reset = true, .resetTo = 4.0f})) {
                            changed = true;
                        }
                        int confidenceBlurRadius = static_cast<int>(state->debug.restir.confidenceBlurRadius);
                        if (Widgets::SliderInt("Confidence Blur##restir", &confidenceBlurRadius, 0, 6,
                                               {.tooltip = "Gradient blur radius (in downsampled gradient texels). Wider = smoother penumbra confidence, less noise; too wide blurs the antilag region."})) {
                            state->debug.restir.confidenceBlurRadius = static_cast<uint32_t>(confidenceBlurRadius);
                            changed = true;
                        }
                    });
                    ImGui::EndDisabled();

                    DrawRELAXParamsUI(changed, relax, "main_relax", true);
                }

                if (restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::NRD) {
                    DrawRELAXParamsUI(changed, restir.relax, "main_relax", true, true);
                }

                if (restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::ReBLUR) {
                    DrawReBLURParamsUI(changed, restir.reblur);
                }

                if (restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::NRDReBLUR) {
                    DrawReBLURParamsUI(changed, restir.reblur, true);
                }
            }
        }

        if (changed && state->projectConfig.bAutoSaveLighting) {
            SaveLightingTab(state);
        }
    }
    ImGui::End();
}

static void DrawScreenFadeConfig(Core::ScreenFadeState& fade)
{
    const char* modes[] = {"None", "Fade", "Iris", "Wipe", "Dissolve", "Letterbox"};
    int currentMode = static_cast<int>(fade.mode);
    if (ImGui::Combo("Fade Mode", &currentMode, modes, IM_ARRAYSIZE(modes))) {
        fade.mode = static_cast<Core::ScreenFadeMode>(currentMode);
    }
    if (fade.mode == Core::ScreenFadeMode::None) { return; }

    Widgets::SliderFloat("Progress", &fade.progress, 0.0f, 1.0f, {.reset = true, .resetTo = 0.0f});
    Widgets::SliderFloat("Softness", &fade.softness, 0.0f, 0.5f, {.reset = true, .resetTo = 0.03f});
    ImGui::ColorEdit3("Fade Color", &fade.color.x);
    ImGui::Checkbox("Draw Over UI", &fade.bDrawOverUI);
    if (fade.mode == Core::ScreenFadeMode::Iris || fade.mode == Core::ScreenFadeMode::Dissolve) {
        ImGui::DragFloat2("Center", &fade.center.x, 0.01f, -1.0f, 2.0f);
    }
    if (fade.mode == Core::ScreenFadeMode::Wipe) {
        ImGui::DragFloat2("Direction", &fade.direction.x, 0.01f, -1.0f, 1.0f);
    }
}

bool DrawPostProcessConfig(Core::PostProcessConfiguration& pp)
{
    constexpr Core::PostProcessConfiguration defaults{};
    bool changed = false;

    auto check = [&](const char* label, bool* v) {
        if (ImGui::Checkbox(label, v)) { changed = true; }
    };
    auto ppF = [&](const char* label, float* v, float def, float mn, float mx, const char* fmt = "%.3f", const char* tip = nullptr) {
        changed |= Widgets::SliderFloat(label, v, mn, mx, {.format = fmt, .tooltip = tip, .reset = true, .resetTo = def});
    };

    if (ImGui::CollapsingHeader("Tonemapping")) {
        const char* tonemapOperators[] = {"None", "[Simple] ACES (Hill)", "[Simple] Hable", "[Simple] Reinhard", "[Simple] Lottes", "[Simple] Reinhard-Jodie", "[Simple] Clamp", "[Filmic] Hejl-Burgess-Dawson", "[Filmic] Uchimura", "[Filmic] ACES (Narkowicz)", "[Modern] AgX", "[Modern] Khronos PBR Neutral"};
        int currentItem = pp.tonemapOperator + 1;
        if (ImGui::Combo("Operator", &currentItem, tonemapOperators, IM_ARRAYSIZE(tonemapOperators))) {
            pp.tonemapOperator = currentItem - 1;
            changed = true;
        }
        switch (pp.tonemapOperator) {
            case 1:
                ppF("White Point##hable", &pp.hableParams.whitePoint, defaults.hableParams.whitePoint, 1.0f, 20.0f, "%.2f");
                break;
            case 2:
                ppF("White Point##reinhard", &pp.reinhardParams.whitePoint, defaults.reinhardParams.whitePoint, 1.0f, 20.0f, "%.2f");
                break;
            case 7:
                ppF("Max Brightness##uchimura", &pp.uchimuraParams.P, defaults.uchimuraParams.P, 0.5f, 2.0f, "%.2f");
                ppF("Contrast##uchimura", &pp.uchimuraParams.a, defaults.uchimuraParams.a, 0.5f, 2.0f, "%.2f");
                ppF("Linear Start##uchimura", &pp.uchimuraParams.m, defaults.uchimuraParams.m, 0.0f, 0.5f, "%.3f");
                ppF("Linear Length##uchimura", &pp.uchimuraParams.l, defaults.uchimuraParams.l, 0.0f, 1.0f, "%.2f");
                ppF("Toe Power##uchimura", &pp.uchimuraParams.c, defaults.uchimuraParams.c, 0.5f, 3.0f, "%.2f");
                ppF("Pedestal##uchimura", &pp.uchimuraParams.b, defaults.uchimuraParams.b, 0.0f, 0.1f, "%.3f");
                break;
            case 9:
                ppF("Min EV##agx", &pp.agxParams.minEV, defaults.agxParams.minEV, -20.0f, -1.0f, "%.3f");
                ppF("Max EV##agx", &pp.agxParams.maxEV, defaults.agxParams.maxEV, 0.0f, 10.0f, "%.3f");
                break;
            case 10:
                ppF("Start Compression##khronos", &pp.khronosParams.startCompression, defaults.khronosParams.startCompression, 0.5f, 0.95f, "%.3f");
                ppF("Desaturation##khronos", &pp.khronosParams.desaturation, defaults.khronosParams.desaturation, 0.0f, 0.5f, "%.3f");
                break;
            default:
                break;
        }
    }

    if (ImGui::CollapsingHeader("Exposure")) {
        check("Enabled##exposure", &pp.bExposureEnabled);
        ppF("Target Luminance", &pp.exposureTargetLuminance, defaults.exposureTargetLuminance, 0.005f, 1.0f, "%.3f", "Post-exposure key the metered scene average maps to. 0.18 = standard mid-gray.");
        ppF("Speed Brighten", &pp.exposureSpeedBrighten, defaults.exposureSpeedBrighten, 0.1f, 10.0f, "%.1f", "Adaptation speed (1/s) while the image brightens (entering darkness). Slower than darken, like the eye.");
        ppF("Speed Darken", &pp.exposureSpeedDarken, defaults.exposureSpeedDarken, 0.1f, 10.0f, "%.1f", "Adaptation speed (1/s) while the image darkens (entering light).");
        ppF("Min Gain EV", &pp.exposureMinGainEV, defaults.exposureMinGainEV, -12.0f, 0.0f, "%.1f", "Lower bound on exposure gain in stops; limits how far bright scenes are darkened.");
        ppF("Max Gain EV", &pp.exposureMaxGainEV, defaults.exposureMaxGainEV, 0.0f, 12.0f, "%.1f", "Upper bound on exposure gain in stops; dark scenes stop brightening here instead of amplifying GI noise to mid-gray.");
        ppF("Low Percentile", &pp.exposureLowPercentile, defaults.exposureLowPercentile, 0.0f, 0.9f, "%.2f", "Fraction of the darkest non-black pixels excluded from metering.");
        ppF("High Percentile", &pp.exposureHighPercentile, defaults.exposureHighPercentile, 0.1f, 1.0f, "%.2f", "Metering cutoff for the brightest pixels; keeps fireflies, emissives, and the sun from steering exposure.");
    }

    if (ImGui::CollapsingHeader("Bloom")) {
        check("Enabled##bloom", &pp.bBloomEnabled);
        ppF("Intensity", &pp.bloomIntensity, defaults.bloomIntensity, 0.0f, 1.0f);
        ppF("Threshold", &pp.bloomThreshold, defaults.bloomThreshold, 0.0f, 2.0f, "%.2f", "Display-relative luminance where bloom starts; 1.0 = displayed white. Stable under auto-exposure.");
        ppF("Soft Threshold", &pp.bloomSoftThreshold, defaults.bloomSoftThreshold, 0.0f, 1.0f, "%.2f");
        ppF("Radius", &pp.bloomRadius, defaults.bloomRadius, 0.5f, 1.25f, "%.2f", "Tent-filter tap spacing in mip texels; above ~1.25 the upsample starts skipping texels and shimmers.");
        ppF("Clamp", &pp.bloomClamp, defaults.bloomClamp, 0.1f, 100.0f, "%.1f", "Display-relative cap applied before thresholding; secondary firefly defense behind the Karis average.");
    }

    if (ImGui::CollapsingHeader("Motion Blur")) {
        check("Enabled##motionblur", &pp.bMotionBlurEnabled);
        check("Object Only##motionblur", &pp.bMotionBlurObjectOnly);
        ImGui::SetItemTooltip("Subtract the camera-only reprojection so only moving objects smear. Off = full camera + object blur.");
        ppF("Velocity Scale", &pp.motionBlurVelocityScale, defaults.motionBlurVelocityScale, 0.0f, 2.0f, "%.2f", "Shutter fraction of the inter-frame displacement; 0.5 = cinematic 180-degree shutter.");
        ppF("Target FPS", &pp.motionBlurTargetFps, defaults.motionBlurTargetFps, 0.0f, 240.0f, "%.0f", "Frame rate the shutter is normalized to, so blur length stays constant as fps varies and hitches do not smear. 0 = physical shutter (blur grows with frame time).");
        ppF("Depth Scale", &pp.motionBlurDepthScale, defaults.motionBlurDepthScale, 0.1f, 10.0f, "%.2f", "1 / soft depth band (view units) for foreground/background classification. 1.0 = 1m band.");
    }

    if (ImGui::CollapsingHeader("Color Grading")) {
        check("Enabled##colorgrading", &pp.bColorGradingEnabled);
        ppF("Exposure Bias", &pp.colorGradingExposure, defaults.colorGradingExposure, -2.0f, 2.0f, "%.2f", "EV bias folded into exposure before tonemapping, so highlights roll off instead of clipping.");
        ppF("Contrast", &pp.colorGradingContrast, defaults.colorGradingContrast, 0.5f, 2.0f, "%.2f", "Log-space contrast pivoted at mid-gray.");
        ppF("Saturation", &pp.colorGradingSaturation, defaults.colorGradingSaturation, 0.0f, 2.0f, "%.2f");
        ppF("Temperature", &pp.colorGradingTemperature, defaults.colorGradingTemperature, -1.0f, 1.0f, "%.2f", "White balance warm/cool via CAT02 gains; preserves black and overall luminance.");
        ppF("Tint", &pp.colorGradingTint, defaults.colorGradingTint, -1.0f, 1.0f, "%.2f", "White balance green/magenta axis.");
    }

    if (ImGui::CollapsingHeader("Vignette")) {
        check("Enabled##vignette", &pp.bVignetteEnabled);
        ppF("Strength##vignette", &pp.vignetteStrength, defaults.vignetteStrength, 0.0f, 1.0f, "%.2f");
        ppF("Radius##vignette", &pp.vignetteRadius, defaults.vignetteRadius, 0.5f, 1.0f, "%.2f");
        ppF("Smoothness##vignette", &pp.vignetteSmoothness, defaults.vignetteSmoothness, 0.05f, 1.0f, "%.2f");
        ppF("Roundness##vignette", &pp.vignetteRoundness, defaults.vignetteRoundness, 0.0f, 1.0f, "%.2f", "0 = screen-fit ellipse, 1 = circular on any aspect ratio.");
    }

    if (ImGui::CollapsingHeader("Chromatic Aberration")) {
        check("Enabled##chromab", &pp.bChromaticAberrationEnabled);
        ppF("Strength##chromab", &pp.chromaticAberrationStrength, defaults.chromaticAberrationStrength, 0.0f, 10.0f, "%.2f", "Pixels of R/B separation at unit radius, uniform in all directions. Above ~4 the single-tap fringes read as ghosting.");
    }

    if (ImGui::CollapsingHeader("Sharpening")) {
        check("Enabled##sharpening", &pp.bSharpeningEnabled);
        ppF("Strength##sharpening", &pp.sharpeningStrength, defaults.sharpeningStrength, 0.0f, 1.0f, "%.2f", "Display-referred unsharp mask with a bounded delta; runs after tonemapping so strength is perceptually uniform.");
    }

    if (ImGui::CollapsingHeader("Panini Projection")) {
        check("Enabled##panini", &pp.bPaniniEnabled);
        ppF("Strength##panini", &pp.paniniStrength, defaults.paniniStrength, 0.0f, 1.0f, "%.2f", "Cylindrical projection blend; 0 = rectilinear. Typical game values 0.15-0.35.");
    }

    if (ImGui::CollapsingHeader("Film Grain")) {
        check("Enabled##filmgrain", &pp.bFilmGrainEnabled);
        ppF("Strength##grain", &pp.grainStrength, defaults.grainStrength, 0.0f, 0.05f);
        ppF("Size##grain", &pp.grainSize, defaults.grainSize, 1.0f, 4.0f, "%.2f", "Grain cell size in pixels.");
        ppF("Response##grain", &pp.grainResponse, defaults.grainResponse, 0.0f, 1.0f, "%.2f", "0 = flat video-style noise, 1 = film response (strongest in mids, vanishing in crushed blacks and clipped whites).");
    }

    if (ImGui::CollapsingHeader("Dither")) {
        check("Enabled##dither", &pp.bDitherEnabled);
        ppF("Strength##dither", &pp.ditherStrength, defaults.ditherStrength, 0.0f, 2.0f, "%.2f", "Amplitude in encoded 8-bit LSBs, matched to the sRGB step at each pixel. Auto-disabled while render scale rescales the output.");
    }

    return changed;
}

void DrawPostProcessingWindow(Engine::EngineState* state)
{
    if (ImGui::Begin("Post-Processing")) {
        bool changed = false;

        if (Widgets::SaveBar("pp", &state->projectConfig.bAutoSavePostProcess)) {
            SavePostProcessTab(state);
        }
        DrawPostProcessProfiles(state);

        ImGui::Separator();
        if (ImGui::Button("Reset All to Defaults")) {
            state->lighting.postProcess = Core::PostProcessConfiguration{};
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable All Effects")) {
            Core::PostProcessConfiguration& pp = state->lighting.postProcess;
            pp.tonemapOperator = -1;
            pp.bExposureEnabled = false;
            pp.bBloomEnabled = false;
            pp.bMotionBlurEnabled = false;
            pp.bColorGradingEnabled = false;
            pp.bVignetteEnabled = false;
            pp.bChromaticAberrationEnabled = false;
            pp.bSharpeningEnabled = false;
            pp.bPaniniEnabled = false;
            pp.bFilmGrainEnabled = false;
            pp.bDitherEnabled = false;
            changed = true;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Image Effects");
        changed |= DrawPostProcessConfig(state->lighting.postProcess);

        if (changed && state->projectConfig.bAutoSavePostProcess) {
            SavePostProcessTab(state);
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Screen Fade");
        DrawScreenFadeConfig(state->screenFade);
    }
    ImGui::End();
}
}
