//
// Created by William on 2026-06-13.
//

#include "graphics_settings.h"

#include "imgui.h"

#include "game/editor/editor_widgets.h"

#include "game/systems/debug_system.h"
#include "core/string_id.h"
#include "core/containers/arena_array.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "engine/profiles/profile_library.h"

namespace Game
{
static void SaveProjectConfigTab(Engine::EngineState* state)
{
    Engine::ProjectConfig& cfg = state->projectConfig;
    cfg.lightingMode = state->lighting.lightingMode;
    cfg.aaConfig = state->lighting.aaConfig;
    Engine::WriteProjectConfig(cfg);
}

static void SaveLightingTab(Engine::EngineState* state)
{
    Engine::ProjectConfig& cfg = state->projectConfig;
    if (!cfg.activeLightingProfile.IsEmpty()) {
        Engine::Profiles::SaveLightingProfile(cfg.activeLightingProfile.c_str(), state->debug.restir, state->lighting.gtaoConfig);
    }
    cfg.restir = state->debug.restir;
    cfg.gtaoConfig = state->lighting.gtaoConfig;
    Engine::WriteProjectConfig(cfg);
}

static void SavePostProcessTab(Engine::EngineState* state)
{
    Engine::ProjectConfig& cfg = state->projectConfig;
    if (!cfg.activePostProcessProfile.IsEmpty()) {
        Engine::Profiles::SavePostProcessProfile(cfg.activePostProcessProfile.c_str(), state->lighting.postProcess);
    }
    cfg.postProcess = state->lighting.postProcess;
    Engine::WriteProjectConfig(cfg);
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
                Engine::Profiles::LoadLightingProfile(names[i].c_str(), state->debug.restir, state->lighting.gtaoConfig);
                cfg.restir = state->debug.restir;
                cfg.gtaoConfig = state->lighting.gtaoConfig;
                Engine::WriteProjectConfig(cfg);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(cfg.activeLightingProfile.IsEmpty());
    if (ImGui::Button("Delete##lightingprofile")) {
        Engine::Profiles::DeleteLightingProfile(cfg.activeLightingProfile.c_str());
        cfg.activeLightingProfile = Core::InlineString<64>();
        Engine::WriteProjectConfig(cfg);
    }
    ImGui::EndDisabled();

    static char lightingNewName[64] = "";
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("##lightingnewname", lightingNewName, sizeof(lightingNewName));
    ImGui::SameLine();
    if (ImGui::Button("Save As##lightingprofile") && lightingNewName[0] != '\0') {
        Engine::Profiles::SaveLightingProfile(lightingNewName, state->debug.restir, state->lighting.gtaoConfig);
        cfg.activeLightingProfile = Core::InlineString<64>(lightingNewName);
        Engine::WriteProjectConfig(cfg);
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
                cfg.postProcess = state->lighting.postProcess;
                Engine::WriteProjectConfig(cfg);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(cfg.activePostProcessProfile.IsEmpty());
    if (ImGui::Button("Delete##ppprofile")) {
        Engine::Profiles::DeletePostProcessProfile(cfg.activePostProcessProfile.c_str());
        cfg.activePostProcessProfile = Core::InlineString<64>();
        Engine::WriteProjectConfig(cfg);
    }
    ImGui::EndDisabled();

    static char ppNewName[64] = "";
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("##ppnewname", ppNewName, sizeof(ppNewName));
    ImGui::SameLine();
    if (ImGui::Button("Save As##ppprofile") && ppNewName[0] != '\0') {
        Engine::Profiles::SavePostProcessProfile(ppNewName, state->lighting.postProcess);
        cfg.activePostProcessProfile = Core::InlineString<64>(ppNewName);
        Engine::WriteProjectConfig(cfg);
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

        const char* lightingModeLabels[] = {"Default", "ReSTIR", "Ground-Truth ReSTIR", "Path Tracing"};
        Core::LightingMode prevLightingMode = state->lighting.lightingMode;
        int32_t lightingModeIndex = static_cast<int32_t>(state->lighting.lightingMode);
        if (ImGui::Combo("Lighting Mode", &lightingModeIndex, lightingModeLabels, 4)) {
            state->lighting.lightingMode = static_cast<Core::LightingMode>(lightingModeIndex);
            changed = true;
            if (prevLightingMode != Core::LightingMode::GroundTruthReSTIR && state->lighting.lightingMode == Core::LightingMode::GroundTruthReSTIR) {
                state->lighting.bResetGroundTruth = true;
            }
        }

        ImGui::Spacing();

        if (ImGui::Checkbox("Limit FPS", &state->projectConfig.bLimitFps)) { changed = true; }
        if (state->projectConfig.bLimitFps) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            if (Widgets::SliderInt("##fps_cap", &state->projectConfig.frameLimitTarget, 15, 240)) { changed = true; }
        }

        ImGui::Spacing();

        auto bIsGroundTruth = state->lighting.lightingMode == Core::LightingMode::GroundTruthReSTIR;
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


        const char* aaModes[] = {"None", "SMAA", "TAA", "SMAA T2X", "Naive TAA"};
        int currentAA = static_cast<int>(state->lighting.aaConfig.mode);
        if (ImGui::Combo("Anti-Aliasing", &currentAA, aaModes, IM_ARRAYSIZE(aaModes))) {
            state->lighting.aaConfig.mode = static_cast<Core::AntiAliasingMode>(currentAA);
            changed = true;
        }

        const Core::AntiAliasingMode aaMode = state->lighting.aaConfig.mode;
        const bool bSMAA = aaMode == Core::AntiAliasingMode::SMAA || aaMode == Core::AntiAliasingMode::SMAAT2X;
        const bool bTAA = aaMode == Core::AntiAliasingMode::TAA || aaMode == Core::AntiAliasingMode::NaiveTAA;

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
            if (ImGui::Button("Reset TAA")) {
                taa = defaultTAA;
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
        ImGui::Checkbox("Wireframe", &state->debug.bWireframe);

        ImGui::Separator();

        ImGui::Text("Current Debug View: %s", state->debug.resourceName.IsEmpty() ? "None" : state->debug.resourceName.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Disable Debug View")) {
            state->debug.resourceName.Clear();
        }
        ImGui::Checkbox("Enable V-Buffer Shade Dispatch Bucketing Visualization", &state->debug.bEnableShadeDispatchBucketingVisualization);
        ImGui::Checkbox("Enable V-Buffer Lighting Bucketing Visualization", &state->debug.bEnableLightingBucketingVisualization);

        ImGui::Separator();

        ImGui::BeginDisabled(true);
        ImGui::Checkbox("Enable Portals", &state->debug.bEnablePortal);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Portals are not currently functional");
        }
        ImGui::EndDisabled();


        ImGui::Separator();

        if (ImGui::CollapsingHeader("Hotkeys", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* keyNames[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
            for (size_t i = 0; i < std::size(DEBUG_HOTKEYS); ++i) {
                ImGui::Text("%s: %s (%s)", keyNames[i], DEBUG_HOTKEYS[i].name, DEBUG_HOTKEYS[i].resourceName);
            }
        }

        ImGui::Separator();

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

        if (ImGui::CollapsingHeader("Anti-Aliasing")) {
            if (ImGui::Button("TAA Current")) setDebugTarget("taa_current", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("TAA Output")) setDebugTarget("taa_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::Separator();
            if (ImGui::Button("SMAA Edges")) setDebugTarget("smaa_edges", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("SMAA Blend Weights")) setDebugTarget("smaa_blend", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("SMAA Output")) setDebugTarget("smaa_output", DebugTransformationType::None, Core::DebugViewAspect::None);
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
}

void DrawLightingWindow(Engine::EngineState* state)
{
    if (ImGui::Begin("Lighting")) {
        bool changed = false;

        if (Widgets::SaveBar("lighting", &state->projectConfig.bAutoSaveLighting)) {
            SaveLightingTab(state);
        }
        DrawLightingProfiles(state);

        ImGui::Separator();

        if (ImGui::CollapsingHeader("ReSTIR DI Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            Core::ReSTIRParams& restir = state->debug.restir;
            ImGui::Separator();
            if (ImGui::Checkbox("Half Res", &restir.bHalfRes)) {
                changed = true;
            }
            ImGui::Separator();
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
            int temporalMCap = static_cast<int>(restir.temporalMCap);
            if (Widgets::SliderInt("Temporal M Cap", &temporalMCap, 1, 2000)) {
                restir.temporalMCap = static_cast<uint32_t>(temporalMCap);
                changed = true;
            }
            if (ImGui::Checkbox("Adaptive Spatial", &restir.bAdaptiveSpatial)) {
                changed = true;
            }
            if (restir.bAdaptiveSpatial && Widgets::SliderFloat("Adaptive Boost##restir", &restir.adaptiveSpatialBoost, 0.0f, 3.0f)) {
                changed = true;
            }
            if (Widgets::SliderFloat("Antilag Strength##restir", &restir.antilagStrength, 0.0f, 1.0f,
                    {.format = "%.2f", .tooltip = "Shrinks carried temporal M where the shadow term flipped vs reprojected history, so moving shadows lose their ghost trail. Combined-temporal mode only; may add noise in soft-shadow boundaries.", .reset = true, .resetTo = 0.0f})) {
                changed = true;
            }
            ImGui::Separator();
            if (Widgets::SliderFloat("IBL Intensity##restir", &restir.iblIntensity, 0.0f, 2.0f)) {
                changed = true;
            }
            ImGui::Separator();
            const char* modeLabels[] = {"Main + Temporal", "Combined Temporal"};
            int modeIdx = static_cast<int>(restir.mode);
            if (ImGui::Combo("ReSTIR Mode", &modeIdx, modeLabels, 2)) {
                restir.mode = static_cast<Core::ReSTIRParams::Mode>(modeIdx);
                changed = true;
            }
            int spatialPasses = static_cast<int>(restir.spatialPasses);
            if (Widgets::SliderInt("Spatial Passes", &spatialPasses, 1, 8)) {
                restir.spatialPasses = static_cast<uint32_t>(spatialPasses);
                changed = true;
            }
            ImGui::Separator();
            if (ImGui::Checkbox("Permutation Sampling", &restir.bPermutationSampling)) {
                changed = true;
            }
            ImGui::Separator();
            const char* stopLabels[] = {"After Spatial 1", "After Temporal", "After Generate"};
        }

        if (ImGui::CollapsingHeader("Denoiser")) {
            Core::ReSTIRParams& restir = state->debug.restir;
            const char* denoiserModes[] = {"None", "A-Trous Wavelet", "A-SVGF", "RELAX"};
            int currentDenoiser = static_cast<int>(restir.denoiserMode);
            if (ImGui::Combo("Mode##denoiser", &currentDenoiser, denoiserModes, IM_ARRAYSIZE(denoiserModes))) {
                restir.denoiserMode = static_cast<Core::ReSTIRParams::DenoiserMode>(currentDenoiser);
                changed = true;
            }

            const char* remodulateOutputModes[] = {"Both", "Diffuse Only", "Specular Only"};
            int currentRemodulateOutput = static_cast<int>(restir.remodulateOutput);
            if (ImGui::Combo("Remodulate Output##restir", &currentRemodulateOutput, remodulateOutputModes, IM_ARRAYSIZE(remodulateOutputModes))) {
                restir.remodulateOutput = static_cast<Core::ReSTIRParams::RemodulateOutput>(currentRemodulateOutput);
                changed = true;
            }

            const bool bATrous = restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::ATrous;
            const bool bSVGF = restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::ASVGF;
            const bool bRELAX = restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX;

            if (bATrous) {
                ImGui::SeparatorText("A-Trous");
                if (Widgets::SliderInt("Iterations##atrous", &restir.atrous.iterations, 1, 4)) { changed = true; }
                if (Widgets::SliderFloat("Sigma Luminance##atrous", &restir.atrous.sigmaLuminance, 0.0f, 10.0f)) { changed = true; }
                if (Widgets::SliderFloat("Sigma Normal##atrous", &restir.atrous.sigmaNormal, 1.0f, 256.0f)) { changed = true; }
                if (Widgets::SliderFloat("Sigma Depth##atrous", &restir.atrous.sigmaDepth, 0.0001f, 1.0f)) { changed = true; }
                if (ImGui::Button("Reset A-Trous")) {
                    restir.atrous = Core::ReSTIRParams::ATrousParams{};
                    changed = true;
                }
            }
            if (bSVGF) {
                ImGui::SeparatorText("A-SVGF");
                if (Widgets::SliderInt("ATrous Iterations##svgf", &restir.svgf.atrousIterations, 0, 4)) { changed = true; }
                if (Widgets::SliderFloat("Alpha Min##svgf", &restir.svgf.alphaMin, 0.005f, 1.0f)) { changed = true; }
                if (Widgets::SliderFloat("Gradient Threshold##svgf", &restir.svgf.gradientThreshold, 0.0f, 0.2f)) { changed = true; }
                if (Widgets::SliderFloat("Sigma Luminance##svgf", &restir.svgf.sigmaLuminance, 0.1f, 20.0f)) { changed = true; }
                if (Widgets::SliderFloat("Sigma Normal##svgf", &restir.svgf.sigmaNormal, 1.0f, 256.0f)) { changed = true; }
                if (Widgets::SliderFloat("Sigma Depth##svgf", &restir.svgf.sigmaDepth, 0.0001f, 1.0f)) { changed = true; }
                if (ImGui::Button("Reset A-SVGF")) {
                    restir.svgf = Core::ReSTIRParams::SVGFParams{};
                    changed = true;
                }
            }
            if (bRELAX) {
                Core::RELAXParams& relax = state->debug.restir.relax;
                ImGui::SeparatorText("RELAX");

                if (Widgets::SliderFloat("History Confidence##restir", &state->debug.restir.confidenceStrength, 0.0f, 1.0f,
                        {.format = "%.2f", .tooltip = "Moving-shadow antilag from ReSTIR: drops RELAX history confidence where the shadow term flipped vs reprojected history (0 disables). Combined-temporal mode only.", .reset = true, .resetTo = 0.75f})) {
                    changed = true;
                }

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
                relaxF("Disocclusion Threshold", &relax.disocclusionThreshold, relaxDefaults.disocclusionThreshold, 0.001f, 0.05f, "%.4f", "Relative depth tolerance for accepting reprojected history. Higher accepts more (less ghosting rejection); lower resets more on edges/motion. Default 0.005; common ~0.01.");
                relaxF("Depth Threshold", &relax.depthThreshold, relaxDefaults.depthThreshold, 0.0f, 0.05f, "%.4f", "Plane-distance tolerance for spatial edge stopping, as a fraction of depth. Lower preserves geometry edges; higher blurs across them. Default 0.003.");

                ImGui::SeparatorText("Accumulation");
                relaxF("Spec Max Accum Frames", &relax.specMaxAccumFrames, relaxDefaults.specMaxAccumFrames, 0.f, 64.f, "%.0f", "Max specular history length (stable). Higher = cleaner but laggier reflections. Default 32; common 30-60.");
                relaxF("Spec Max Fast Accum Frames", &relax.specMaxFastAccumFrames, relaxDefaults.specMaxFastAccumFrames, 0.f, 16.f, "%.0f", "Length of the noisy 'fast' specular history used to clamp the slow one (anti-lag). Must be below Spec Max Accum to enable clamping. Default 4; common 4-6.");
                relaxF("Diff Max Accum Frames", &relax.diffMaxAccumFrames, relaxDefaults.diffMaxAccumFrames, 0.f, 64.f, "%.0f", "Max diffuse history length (stable). Higher = cleaner but slower to react to lighting changes (more lag). Default 32; common 30-60.");
                relaxF("Diff Max Fast Accum Frames", &relax.diffMaxFastAccumFrames, relaxDefaults.diffMaxFastAccumFrames, 0.f, 16.f, "%.0f", "Length of the noisy 'fast' diffuse history used to clamp the slow one (anti-lag). Lower = snappier response. Must be below Diff Max Accum. Default 4; common 4-6.");
                relaxF("History Acceleration Amount", &relax.historyAccelerationAmount, relaxDefaults.historyAccelerationAmount, 0.f, 1.f, "%.2f", "Strength of anti-lag acceleration pushing the slow history toward the fast one on changes. 0 = off, 1 = max. Default 1.0.");

                ImGui::SeparatorText("Prepass");
                relaxF("Diff Blur Radius", &relax.diffBlurRadius, relaxDefaults.diffBlurRadius, 0.f, 100.f, "%.1f", "Radius (px) of the diffuse pre-blur applied before accumulation. Larger knocks down more input noise but loses detail. 0 disables. Default 30.");
                relaxF("Spec Blur Radius", &relax.specBlurRadius, relaxDefaults.specBlurRadius, 0.f, 100.f, "%.1f", "Radius (px) of the specular pre-blur before accumulation. 0 disables. Default 50.");
                relaxF("Min Hit Distance Weight", &relax.minHitDistanceWeight, relaxDefaults.minHitDistanceWeight, 0.f, 1.f, "%.2f", "Minimum weight for ray hit-distance when reconstructing specular in the prepass. 0 ignores hitT. Default 0; NRD commonly ~0.1-0.2.");

                ImGui::SeparatorText("A-Trous / Edge Stopping");
                relaxI("ATrous Iterations", &relax.atrousIterations, relaxDefaults.atrousIterations, 1, 5, "Number of A-trous wavelet (spatial) passes. More = wider, smoother denoising but blurrier and costlier. Default 3; common 4-5.");
                relaxF("Lobe Angle Fraction", &relax.lobeAngleFraction, relaxDefaults.lobeAngleFraction, 0.f, 1.f, "%.3f", "Normal edge-stopping tolerance, as a fraction of the BRDF lobe angle. Lower preserves sharper normal detail; higher blurs across normals. Default 0.15.");
                relaxF("Roughness Fraction", &relax.roughnessFraction, relaxDefaults.roughnessFraction, 0.f, 1.f, "%.3f", "Roughness edge-stopping tolerance (fraction). Higher blends across differing roughness; lower keeps roughness boundaries crisp. Default 0.15.");
                relaxF("Spec Lobe Angle Slack", &relax.specLobeAngleSlack, relaxDefaults.specLobeAngleSlack, 0.f, 1.f, "%.3f", "Extra angular slack added to the specular lobe for edge stopping, loosening normal/view rejection. Default 0.15.");
                relaxF("Spec Phi Luminance", &relax.specPhiLuminance, relaxDefaults.specPhiLuminance, 0.f, 10.f, "%.2f", "Specular luminance edge-stopping sensitivity (sigma scale). Higher = more blur (ignores luminance diffs); lower preserves highlights. Default 2.0; common 1-2.");
                relaxF("Diff Phi Luminance", &relax.diffPhiLuminance, relaxDefaults.diffPhiLuminance, 0.f, 10.f, "%.2f", "Diffuse luminance edge-stopping sensitivity (sigma scale). Higher = more blur; lower keeps luminance edges. Default 2.0; common 1-2.");
                relaxF("Diff Max Lum Rel Diff", &relax.diffMaxLuminanceRelativeDifference, relaxDefaults.diffMaxLuminanceRelativeDifference, 0.f, 10.f, "%.2f", "Caps how strongly a luminance difference can reject a diffuse sample (in sigmas). Lower = firmer edge stopping. Default 3.");
                relaxF("Spec Max Lum Rel Diff", &relax.specMaxLuminanceRelativeDifference, relaxDefaults.specMaxLuminanceRelativeDifference, 0.f, 10.f, "%.2f", "Caps how strongly a luminance difference can reject a specular sample (in sigmas). Default 3.");
                relaxF("Luminance Edge Stop Relax", &relax.luminanceEdgeStoppingRelaxation, relaxDefaults.luminanceEdgeStoppingRelaxation, 0.f, 1.f, "%.2f", "On early A-trous passes, relaxes specular luminance edge stopping where reprojection confidence is low (helps fresh/disoccluded pixels). 0-1. Default 0.5.");
                relaxF("Normal Edge Stop Relax", &relax.normalEdgeStoppingRelaxation, relaxDefaults.normalEdgeStoppingRelaxation, 0.f, 1.f, "%.2f", "Relaxes specular normal edge stopping based on reprojection confidence, cutting noise on low-confidence pixels. 0-1. Default 0.3.");
                relaxF("Roughness Edge Stop Relax", &relax.roughnessEdgeStoppingRelaxation, relaxDefaults.roughnessEdgeStoppingRelaxation, 0.f, 1.f, "%.2f", "Relaxes the view vector used in specular weighting, loosening rejection on curved/rough surfaces. Default 0.3.");
                relaxF("Spec Variance Boost", &relax.specVarianceBoost, relaxDefaults.specVarianceBoost, 0.f, 8.f, "%.2f", "Boosts specular variance while history is short so fresh pixels filter more aggressively. 1 = no boost. Default 1.0.");

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
            }
        }

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
            sigmaF("History Weight##sigma", &sigma.historyWeight, sigmaDefaults.historyWeight, 0.0f, 0.95f, "%.2f", "Temporal stabilization strength. Higher = steadier but laggier on moving shadows; lower = snappier but shimmerier. Default 0.8.");
            sigmaF("Max Kernel Pixels##sigma", &sigma.maxKernelPixels, sigmaDefaults.maxKernelPixels, 1.0f, 64.0f, "%.0f", "Cap on the penumbra blur radius (px). Bounds cost on very soft shadows. Default 32.");
            sigmaF("Penumbra Scale##sigma", &sigma.penumbraScale, sigmaDefaults.penumbraScale, 0.0f, 4.0f, "%.2f", "Artistic multiplier on the estimated penumbra. >1 softer, <1 sharper. Default 1.0.");

            if (ImGui::Button("Reset SIGMA")) {
                sigma = Core::SIGMAParams{};
                changed = true;
            }
        }

        if (ImGui::CollapsingHeader("Ground Truth Ambient Occlusion")) {
            if (ImGui::Checkbox("Enable GTAO", &state->lighting.gtaoConfig.bEnabled)) { changed = true; }
        }

        if (changed && state->projectConfig.bAutoSaveLighting) {
            SaveLightingTab(state);
        }
    }
    ImGui::End();
}

bool DrawPostProcessConfig(Core::PostProcessConfiguration& pp)
{
    constexpr Core::PostProcessConfiguration defaults{};
    bool changed = false;

    auto check = [&](const char* label, bool* v) {
        if (ImGui::Checkbox(label, v)) { changed = true; }
    };
    auto slideF = [&](const char* label, float* v, float mn, float mx, const char* fmt = "%.3f") {
        changed |= Widgets::SliderFloat(label, v, mn, mx, {.format = fmt});
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
                slideF("White Point##hable", &pp.hableParams.whitePoint, 1.0f, 20.0f, "%.2f");
                break;
            case 2:
                slideF("White Point##reinhard", &pp.reinhardParams.whitePoint, 1.0f, 20.0f, "%.2f");
                break;
            case 7:
                slideF("Max Brightness##uchimura", &pp.uchimuraParams.P, 0.5f, 2.0f, "%.2f");
                slideF("Contrast##uchimura", &pp.uchimuraParams.a, 0.5f, 2.0f, "%.2f");
                slideF("Linear Start##uchimura", &pp.uchimuraParams.m, 0.0f, 0.5f, "%.3f");
                slideF("Linear Length##uchimura", &pp.uchimuraParams.l, 0.0f, 1.0f, "%.2f");
                slideF("Toe Power##uchimura", &pp.uchimuraParams.c, 0.5f, 3.0f, "%.2f");
                slideF("Pedestal##uchimura", &pp.uchimuraParams.b, 0.0f, 0.1f, "%.3f");
                break;
            case 9:
                slideF("Min EV##agx", &pp.agxParams.minEV, -20.0f, -1.0f, "%.3f");
                slideF("Max EV##agx", &pp.agxParams.maxEV, 0.0f, 10.0f, "%.3f");
                break;
            case 10:
                slideF("Start Compression##khronos", &pp.khronosParams.startCompression, 0.5f, 0.95f, "%.3f");
                slideF("Desaturation##khronos", &pp.khronosParams.desaturation, 0.0f, 0.5f, "%.3f");
                break;
            default:
                break;
        }
    }

    if (ImGui::CollapsingHeader("Exposure")) {
        check("Enabled##exposure", &pp.bExposureEnabled);
        slideF("Target Luminance", &pp.exposureTargetLuminance, 0.01f, 1.0f);
        slideF("Adaptation Speed", &pp.exposureAdaptationRate, 0.1f, 50.0f, "%.1f");
        if (ImGui::Button("Reset Exposure")) {
            pp.exposureTargetLuminance = defaults.exposureTargetLuminance;
            pp.exposureAdaptationRate = defaults.exposureAdaptationRate;
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Bloom")) {
        check("Enabled##bloom", &pp.bBloomEnabled);
        slideF("Intensity", &pp.bloomIntensity, 0.0f, 0.2f);
        slideF("Threshold", &pp.bloomThreshold, 0.0f, 2.0f, "%.2f");
        slideF("Soft Threshold", &pp.bloomSoftThreshold, 0.0f, 1.0f, "%.2f");
        slideF("Radius", &pp.bloomRadius, 0.5f, 2.0f, "%.2f");
        slideF("Clamp", &pp.bloomClamp, 0.1f, 100.0f, "%.1f");
        if (ImGui::Button("Reset Bloom")) {
            pp.bloomIntensity = defaults.bloomIntensity;
            pp.bloomThreshold = defaults.bloomThreshold;
            pp.bloomSoftThreshold = defaults.bloomSoftThreshold;
            pp.bloomRadius = defaults.bloomRadius;
            pp.bloomClamp = defaults.bloomClamp;
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Motion Blur")) {
        slideF("Velocity Scale", &pp.motionBlurVelocityScale, 0.0f, 4.0f, "%.2f");
        slideF("Depth Scale", &pp.motionBlurDepthScale, 2.0f, 10.0f, "%.2f");
        if (ImGui::Button("Reset Motion Blur")) {
            pp.motionBlurVelocityScale = defaults.motionBlurVelocityScale;
            pp.motionBlurDepthScale = defaults.motionBlurDepthScale;
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Color Grading")) {
        check("Enabled##colorgrading", &pp.bColorGradingEnabled);
        slideF("Exposure Offset", &pp.colorGradingExposure, -2.0f, 2.0f, "%.2f");
        slideF("Contrast", &pp.colorGradingContrast, 0.5f, 2.0f, "%.2f");
        slideF("Saturation", &pp.colorGradingSaturation, 0.0f, 2.0f, "%.2f");
        slideF("Temperature", &pp.colorGradingTemperature, -1.0f, 1.0f, "%.2f");
        slideF("Tint", &pp.colorGradingTint, -1.0f, 1.0f, "%.2f");
        if (ImGui::Button("Reset Color Grading")) {
            pp.colorGradingExposure = defaults.colorGradingExposure;
            pp.colorGradingContrast = defaults.colorGradingContrast;
            pp.colorGradingSaturation = defaults.colorGradingSaturation;
            pp.colorGradingTemperature = defaults.colorGradingTemperature;
            pp.colorGradingTint = defaults.colorGradingTint;
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Vignette & Chromatic Aberration")) {
        check("Enabled##vigab", &pp.bVignetteAberrationEnabled);
        slideF("Aberration Strength", &pp.chromaticAberrationStrength, 0.0f, 100.0f, "%.2f");
        slideF("Vignette Strength", &pp.vignetteStrength, 0.0f, 1.0f, "%.2f");
        slideF("Vignette Radius", &pp.vignetteRadius, 0.5f, 1.0f, "%.2f");
        slideF("Vignette Smoothness", &pp.vignetteSmoothness, 0.1f, 1.0f, "%.2f");
        if (ImGui::Button("Reset Vignette & Aberration")) {
            pp.chromaticAberrationStrength = defaults.chromaticAberrationStrength;
            pp.vignetteStrength = defaults.vignetteStrength;
            pp.vignetteRadius = defaults.vignetteRadius;
            pp.vignetteSmoothness = defaults.vignetteSmoothness;
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Sharpening")) {
        check("Enabled##sharpening", &pp.bSharpeningEnabled);
        slideF("Sharpening Strength", &pp.sharpeningStrength, 0.0f, 100.0f, "%.2f");
        if (ImGui::Button("Reset Sharpening")) {
            pp.sharpeningStrength = defaults.sharpeningStrength;
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Panini Projection")) {
        check("Enabled##panini", &pp.bPaniniEnabled);
        slideF("Panini Strength", &pp.paniniStrength, 0.0f, 1.0f, "%.2f");
        if (ImGui::Button("Reset Panini")) {
            pp.paniniStrength = defaults.paniniStrength;
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Film Grain")) {
        check("Enabled##filmgrain", &pp.bFilmGrainEnabled);
        slideF("Grain Strength", &pp.grainStrength, 0.0f, 0.15f);
        slideF("Grain Size", &pp.grainSize, 1.0f, 3.0f, "%.2f");
        if (ImGui::Button("Reset Grain")) {
            pp.grainStrength = defaults.grainStrength;
            pp.grainSize = defaults.grainSize;
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Dither")) {
        check("Enabled##dither", &pp.bDitherEnabled);
        slideF("Dither Strength", &pp.ditherStrength, 0.0f, 4.0f, "%.2f");
        if (ImGui::Button("Reset Dither")) {
            pp.ditherStrength = defaults.ditherStrength;
            changed = true;
        }
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
            pp.bColorGradingEnabled = false;
            pp.bVignetteAberrationEnabled = false;
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
    }
    ImGui::End();
}
}
