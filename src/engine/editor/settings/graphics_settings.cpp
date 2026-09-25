//
// Created by William on 2026-06-13.
//

#include "graphics_settings.h"

#include <cstring>
#include <glm/glm.hpp>

#include "imgui.h"

#include "core/containers/inline_string.h"

#include "engine/editor/editor_widgets.h"

#include "engine/editor/debug_hotkeys.h"
#include "engine/systems/render_systems.h"
#include "engine/systems/scene_system.h"
#include "engine/editor/probe_bake_system.h"
#include "engine/editor/ddgi_converge_boost.h"
#include "engine/components/camera_components.h"
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/components/render/reflection_probe_component.h"
#include "core/string_id.h"
#include "core/containers/arena_array.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "engine/profiles/profile_library.h"
#include "render/passes/ddgi_passes.h"
#include "render/shaders/lights_interop.h"
#include "render/shaders/reflection_interop.h"
#include "render/passes/final_gather_passes.h"

#include "render/shaders/restir_features_macros.h"
#include "render/shaders/ddgi_interop.h"
#include "render/shaders/radiance_cache_interop.h"
#include "render/shaders/restir_interop.h"
#include "render/shaders/world_grid_interop.h"

namespace Engine
{
static void SaveProjectConfigTab(Engine::EngineState* state)
{
    Engine::ProjectConfig& cfg = state->projectConfig;
    cfg.aaConfig = state->lighting.aaConfig;
    Engine::WriteProjectConfig(cfg, state->allocator);
}

using LightingBundle = Engine::Profiles::LightingProfileBundle;
using LightingSectionCopy = void(*)(const LightingBundle& from, LightingBundle& to);

struct LightingBaseline
{
    LightingBundle bundle{};
    Core::InlineString<> name{};
    bool bValid{false};
};

/** Active profile as on disk. */
static LightingBaseline lightingBaseline{};

static void RefreshLightingBaseline(Engine::EngineState* state)
{
    const Core::InlineString<>& name = state->projectConfig.activeLightingProfile;
    if (lightingBaseline.bValid && lightingBaseline.name == name) { return; }
    lightingBaseline.name = name;
    lightingBaseline.bValid = false;
    if (name.IsEmpty()) { return; }
    lightingBaseline.bundle = Engine::Profiles::CaptureLightingProfile(*state);
    lightingBaseline.bValid = Engine::Profiles::LoadLightingProfile(name.c_str(), lightingBaseline.bundle);
}

static void CopyEnvironmentSection(const LightingBundle& from, LightingBundle& to)
{
    to.iblIntensity = from.iblIntensity;
    to.indirectIntensity = from.indirectIntensity;
}

static void CopyReSTIRDenoiserFields(const Core::ReSTIRParams& from, Core::ReSTIRParams& to)
{
    to.denoiserMode = from.denoiserMode;
    to.bEnableConfidence = from.bEnableConfidence;
    to.confidenceStrength = from.confidenceStrength;
    to.confidenceSensitivity = from.confidenceSensitivity;
    to.confidenceDarknessBias = from.confidenceDarknessBias;
    to.confidenceHistoryLength = from.confidenceHistoryLength;
    to.confidenceBlurRadius = from.confidenceBlurRadius;
    to.atrous = from.atrous;
    to.svgf = from.svgf;
    to.relax = from.relax;
    to.reblur = from.reblur;
}

static void CopyDirectLightingSection(const LightingBundle& from, LightingBundle& to)
{
    Core::ReSTIRParams restir = from.restir;
    CopyReSTIRDenoiserFields(to.restir, restir);
    restir.remodulateOutput = to.restir.remodulateOutput;
    restir.bTemporalSearch = to.restir.bTemporalSearch;
    to.restir = restir;
}

static void CopyReSTIRDenoiserSection(const LightingBundle& from, LightingBundle& to)
{
    CopyReSTIRDenoiserFields(from.restir, to.restir);
}

static void CopyGTAOSection(const LightingBundle& from, LightingBundle& to)
{
    to.gtao = from.gtao;
}

static void CopyDDGISection(const LightingBundle& from, LightingBundle& to)
{
    Core::DDGIParams ddgi = from.ddgi;
    ddgi.bDebugDrawVolumes = to.ddgi.bDebugDrawVolumes;
    ddgi.bCascadeSampling = to.ddgi.bCascadeSampling;
    ddgi.bWorldVolumeGridCull = to.ddgi.bWorldVolumeGridCull;
    to.ddgi = ddgi;
}

static void CopyReflectionsSection(const LightingBundle& from, LightingBundle& to)
{
    to.reflection = from.reflection;
}

static void CopyReflectionProbesSection(const LightingBundle& from, LightingBundle& to)
{
    Core::ReflectionProbeConfiguration probe = from.reflectionProbe;
    probe.bDebugDraw = to.reflectionProbe.bDebugDraw;
    probe.bBruteForcePick = to.reflectionProbe.bBruteForcePick;
    to.reflectionProbe = probe;
}

static void CopyDiagnosticsSection(const LightingBundle& from, LightingBundle& to)
{
    to.shadingOverride = from.shadingOverride;
    to.lightingOverride = from.lightingOverride;
    to.restir.remodulateOutput = from.restir.remodulateOutput;
    to.reflectionProbe.bBruteForcePick = from.reflectionProbe.bBruteForcePick;
    to.ddgi.bCascadeSampling = from.ddgi.bCascadeSampling;
    to.ddgi.bWorldVolumeGridCull = from.ddgi.bWorldVolumeGridCull;
}

/**
 * @param baseline null when no profile is active
 * @param copy null for sections not stored in profiles
 */
template<typename T>
static Widgets::SectionHeader MakeProfileSectionHeader(const T* baseline, const T& live, void (*copy)(const T&, T&))
{
    Widgets::SectionHeader header{};
    header.bSaveRevert = true;
    if (copy == nullptr) {
        header.disabledTooltip = "Not stored in profiles.";
        return header;
    }
    if (baseline == nullptr) {
        header.disabledTooltip = "No active profile.";
        return header;
    }
    T merged = *baseline;
    copy(live, merged);
    header.bDirty = !(merged == *baseline);
    header.bCanSave = header.bDirty;
    header.bCanRevert = header.bDirty;
    return header;
}

static Widgets::SectionHeader MakeLightingSectionHeader(const LightingBundle& live, LightingSectionCopy copy)
{
    return MakeProfileSectionHeader(lightingBaseline.bValid ? &lightingBaseline.bundle : nullptr, live, copy);
}

static void HandleLightingSectionAction(Engine::EngineState* state, const Widgets::SectionHeader& header, LightingSectionCopy copy)
{
    if (copy == nullptr || !lightingBaseline.bValid) { return; }

    if (header.action == Widgets::SectionAction::Save) {
        const char* name = state->projectConfig.activeLightingProfile.c_str();
        LightingBundle bundle = lightingBaseline.bundle;
        Engine::Profiles::LoadLightingProfile(name, bundle);
        copy(Engine::Profiles::CaptureLightingProfile(*state), bundle);
        if (Engine::Profiles::SaveLightingProfile(name, bundle, state->allocator)) {
            lightingBaseline.bundle = bundle;
        }
    }
    else if (header.action == Widgets::SectionAction::Revert) {
        LightingBundle live = Engine::Profiles::CaptureLightingProfile(*state);
        copy(lightingBaseline.bundle, live);
        Engine::Profiles::ApplyLightingProfile(*state, live);
    }
}

static void SaveLightingTab(Engine::EngineState* state)
{
    Engine::ProjectConfig& cfg = state->projectConfig;
    if (!cfg.activeLightingProfile.IsEmpty()) {
        const LightingBundle bundle = Engine::Profiles::CaptureLightingProfile(*state);
        if (Engine::Profiles::SaveLightingProfile(cfg.activeLightingProfile.c_str(), bundle, state->allocator)) {
            lightingBaseline.bundle = bundle;
            lightingBaseline.name = cfg.activeLightingProfile;
            lightingBaseline.bValid = true;
        }
    }
    Engine::WriteProjectConfig(cfg, state->allocator);
}

using PostProcessSectionCopy = void(*)(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to);

struct PostProcessBaseline
{
    Core::PostProcessConfiguration config{};
    Core::InlineString<> name{};
    bool bValid{false};
};

/** Active profile as on disk. */
static PostProcessBaseline postProcessBaseline{};

static void RefreshPostProcessBaseline(Engine::EngineState* state)
{
    const Core::InlineString<>& name = state->projectConfig.activePostProcessProfile;
    if (postProcessBaseline.bValid && postProcessBaseline.name == name) { return; }
    postProcessBaseline.name = name;
    postProcessBaseline.bValid = false;
    if (name.IsEmpty()) { return; }
    postProcessBaseline.config = state->lighting.postProcess;
    postProcessBaseline.bValid = Engine::Profiles::LoadPostProcessProfile(name.c_str(), postProcessBaseline.config);
}

static void CopyExposureSection(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to)
{
    to.exposureMode = from.exposureMode;
    to.exposureTargetLuminance = from.exposureTargetLuminance;
    to.exposureSpeedBrighten = from.exposureSpeedBrighten;
    to.exposureSpeedDarken = from.exposureSpeedDarken;
    to.exposureMinEV100 = from.exposureMinEV100;
    to.exposureMaxEV100 = from.exposureMaxEV100;
    to.exposureLowPercentile = from.exposureLowPercentile;
    to.exposureHighPercentile = from.exposureHighPercentile;
    to.exposureManualEV100 = from.exposureManualEV100;
    to.cameraAperture = from.cameraAperture;
    to.cameraShutterInv = from.cameraShutterInv;
    to.cameraISO = from.cameraISO;
}

static void CopyDepthOfFieldSection(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to)
{
    to.bDepthOfFieldEnabled = from.bDepthOfFieldEnabled;
    to.dofFocusDistance = from.dofFocusDistance;
    to.dofFocusRange = from.dofFocusRange;
    to.dofNearTransition = from.dofNearTransition;
    to.dofFarTransition = from.dofFarTransition;
    to.dofNearRadiusPx = from.dofNearRadiusPx;
    to.dofFarRadiusPx = from.dofFarRadiusPx;
}

static void CopyMotionBlurSection(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to)
{
    to.bMotionBlurEnabled = from.bMotionBlurEnabled;
    to.motionBlurVelocityScale = from.motionBlurVelocityScale;
    to.motionBlurTargetFps = from.motionBlurTargetFps;
    to.motionBlurDepthScale = from.motionBlurDepthScale;
    to.motionBlurMaxRadiusPx = from.motionBlurMaxRadiusPx;
    to.motionBlurObjectScale = from.motionBlurObjectScale;
    to.motionBlurCameraRotationScale = from.motionBlurCameraRotationScale;
    to.motionBlurCameraTranslationScale = from.motionBlurCameraTranslationScale;
    to.motionBlurCameraDeadZonePx = from.motionBlurCameraDeadZonePx;
    to.motionBlurCameraMaxRadiusPx = from.motionBlurCameraMaxRadiusPx;
}

static void CopyBloomSection(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to)
{
    to.bBloomEnabled = from.bBloomEnabled;
    to.bloomThreshold = from.bloomThreshold;
    to.bloomSoftThreshold = from.bloomSoftThreshold;
    to.bloomRadius = from.bloomRadius;
    to.bloomIntensity = from.bloomIntensity;
    to.bloomClamp = from.bloomClamp;
}

static void CopyPaniniSection(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to)
{
    to.bPaniniEnabled = from.bPaniniEnabled;
    to.paniniStrength = from.paniniStrength;
}

static void CopyChromaticAberrationSection(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to)
{
    to.bChromaticAberrationEnabled = from.bChromaticAberrationEnabled;
    to.chromaticAberrationStrength = from.chromaticAberrationStrength;
}

static void CopyColorGradingSection(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to)
{
    to.bColorGradingEnabled = from.bColorGradingEnabled;
    to.colorGradingExposure = from.colorGradingExposure;
    to.colorGradingContrast = from.colorGradingContrast;
    to.colorGradingSaturation = from.colorGradingSaturation;
    to.colorGradingTemperature = from.colorGradingTemperature;
    to.colorGradingTint = from.colorGradingTint;
}

static void CopyTonemappingSection(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to)
{
    to.tonemapOperator = from.tonemapOperator;
    to.uchimuraParams = from.uchimuraParams;
    to.hableParams = from.hableParams;
    to.reinhardParams = from.reinhardParams;
    to.agxParams = from.agxParams;
    to.khronosParams = from.khronosParams;
}

static void CopyVignetteSection(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to)
{
    to.bVignetteEnabled = from.bVignetteEnabled;
    to.vignetteStrength = from.vignetteStrength;
    to.vignetteRadius = from.vignetteRadius;
    to.vignetteSmoothness = from.vignetteSmoothness;
    to.vignetteRoundness = from.vignetteRoundness;
}

static void CopySharpeningSection(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to)
{
    to.bSharpeningEnabled = from.bSharpeningEnabled;
    to.sharpeningStrength = from.sharpeningStrength;
}

static void CopyFilmGrainSection(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to)
{
    to.bFilmGrainEnabled = from.bFilmGrainEnabled;
    to.grainStrength = from.grainStrength;
    to.grainSize = from.grainSize;
    to.grainResponse = from.grainResponse;
}

static void CopyDitherSection(const Core::PostProcessConfiguration& from, Core::PostProcessConfiguration& to)
{
    to.bDitherEnabled = from.bDitherEnabled;
    to.ditherStrength = from.ditherStrength;
}

static void HandlePostProcessSectionAction(Engine::EngineState* state, const Widgets::SectionHeader& header, PostProcessSectionCopy copy)
{
    if (copy == nullptr || !postProcessBaseline.bValid) { return; }

    if (header.action == Widgets::SectionAction::Save) {
        const char* name = state->projectConfig.activePostProcessProfile.c_str();
        Core::PostProcessConfiguration config = postProcessBaseline.config;
        Engine::Profiles::LoadPostProcessProfile(name, config);
        copy(state->lighting.postProcess, config);
        if (Engine::Profiles::SavePostProcessProfile(name, config, state->allocator)) {
            postProcessBaseline.config = config;
        }
    }
    else if (header.action == Widgets::SectionAction::Revert) {
        copy(postProcessBaseline.config, state->lighting.postProcess);
    }
}

static void SavePostProcessTab(Engine::EngineState* state)
{
    Engine::ProjectConfig& cfg = state->projectConfig;
    if (!cfg.activePostProcessProfile.IsEmpty()) {
        if (Engine::Profiles::SavePostProcessProfile(cfg.activePostProcessProfile.c_str(), state->lighting.postProcess, state->allocator)) {
            postProcessBaseline.config = state->lighting.postProcess;
            postProcessBaseline.name = cfg.activePostProcessProfile;
            postProcessBaseline.bValid = true;
        }
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
                lightingBaseline.bValid = false;
                Engine::Profiles::LightingProfileBundle bundle = Engine::Profiles::CaptureLightingProfile(*state);
                if (Engine::Profiles::LoadLightingProfile(names[i].c_str(), bundle)) {
                    Engine::Profiles::ApplyLightingProfile(*state, bundle);
                    state->requests.pendingCacheReset = Core::RenderCacheReset::All;
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
        lightingBaseline.bValid = false;
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
                postProcessBaseline.bValid = false;
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
        postProcessBaseline.bValid = false;
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

        ImGui::SeparatorText("Rendering");
        if (ImGui::Checkbox("Limit FPS", &state->projectConfig.bLimitFps)) { changed = true; }
        if (state->projectConfig.bLimitFps) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            if (Widgets::SliderInt("##fps_cap", &state->projectConfig.frameLimitTarget, 15, 240)) { changed = true; }
        }
        if (Widgets::SliderFloat("Render Resolution##graphics", &state->projectConfig.resolutionScale, 0.33f, 1.0f, {.format = "%.3f", .commitOnRelease = true})) { changed = true; }

        const char* aaModes[] = {"None", "SMAA (reference)", "TAA (reference)", "SMAA T2X (reference)", "Naive TAA (reference)", "Donut TAA (reference)", "FSR 2"};
        int currentAA = static_cast<int>(state->lighting.aaConfig.mode);
        if (ImGui::Combo("Anti-Aliasing", &currentAA, aaModes, IM_ARRAYSIZE(aaModes))) {
            state->lighting.aaConfig.mode = static_cast<Core::AntiAliasingMode>(currentAA);
            changed = true;
        }

        const Core::AntiAliasingMode aaMode = state->lighting.aaConfig.mode;
        const bool bSMAA = aaMode == Core::AntiAliasingMode::SMAA || aaMode == Core::AntiAliasingMode::SMAAT2X;
        const bool bTAA = aaMode == Core::AntiAliasingMode::TAA || aaMode == Core::AntiAliasingMode::NaiveTAA;
        const bool bDonutTAA = aaMode == Core::AntiAliasingMode::DonutTAA;
        const bool bFsr2 = aaMode == Core::AntiAliasingMode::FSR2;

        if (bSMAA) {
            ImGui::SeparatorText("SMAA");
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

        if (bTAA) {
            ImGui::SeparatorText("TAA");
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

        if (bDonutTAA) {
            ImGui::SeparatorText("Donut TAA");
            Core::DonutTAAConfiguration& donutTaa = state->lighting.aaConfig.donutTaa;
            constexpr Core::DonutTAAConfiguration defaultDonutTaa{};
            if (Widgets::SliderFloat("Clamping Factor##donuttaa", &donutTaa.clampingFactor, -1.0f, 4.0f, {.format = "%.2f"})) { changed = true; }
            if (Widgets::SliderFloat("New Frame Weight##donuttaa", &donutTaa.newFrameWeight, 0.01f, 1.0f, {.format = "%.3f"})) { changed = true; }
            if (Widgets::SliderFloat("Max Radiance (pqC)##donuttaa", &donutTaa.maxRadiance, 1.0f, 10000.0f, {.format = "%.1f"})) { changed = true; }
            if (ImGui::Checkbox("Catmull-Rom History##donuttaa", &donutTaa.bUseCatmullRom)) { changed = true; }
            if (ImGui::Button("Reset Donut TAA")) {
                donutTaa = defaultDonutTaa;
                changed = true;
            }
        }

        if (bFsr2) {
            ImGui::SeparatorText("FSR 2");
            Core::Fsr2Configuration& fsr2 = state->lighting.aaConfig.fsr2;
            constexpr Core::Fsr2Configuration defaultFsr2{};
            if (ImGui::Checkbox("Sharpen (RCAS)##fsr2", &fsr2.bSharpen)) { changed = true; }
            if (Widgets::SliderFloat("Sharpness##fsr2", &fsr2.sharpness, 0.0f, 1.0f, {.format = "%.2f"})) { changed = true; }
            if (ImGui::Checkbox("Reactive Mask From Overlays##fsr2", &fsr2.bReactiveMask)) { changed = true; }
            if (Widgets::SliderFloat("Reactive Scale##fsr2", &fsr2.reactiveScale, 0.0f, 4.0f, {.format = "%.2f"})) { changed = true; }
            if (Widgets::SliderFloat("Reactive Threshold##fsr2", &fsr2.reactiveThreshold, 0.0f, 1.0f, {.format = "%.2f"})) { changed = true; }
            if (Widgets::SliderFloat("Reflection Reactive##fsr2", &fsr2.reflectionReactive, 0.0f, 1.0f, {.format = "%.2f"})) { changed = true; }
            if (Widgets::SliderFloat("Mip Bias##fsr2", &fsr2.mipBias, -2.0f, 1.0f, {.format = "%.2f"})) { changed = true; }
            if (ImGui::Button("Reset FSR 2")) {
                fsr2 = defaultFsr2;
                changed = true;
            }
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
        }

        if (changed && state->projectConfig.bAutoSaveProjectConfig) {
            SaveProjectConfigTab(state);
        }
    }
    ImGui::End();
}

static const char* EmissiveDispatchStateName(Engine::EmissiveDispatchState dispatchState)
{
    switch (dispatchState) {
        case Engine::EmissiveDispatchState::Live: return "live";
        case Engine::EmissiveDispatchState::EntityHidden: return "hidden";
        case Engine::EmissiveDispatchState::ProbeBakeHidden: return "probe-hidden";
    }
    return "?";
}

static void DrawEmissiveTriLightSection(Engine::EngineState* state)
{
    Engine::EmissiveDebugState& emissive = state->debug.emissive;
    const uint32_t triLightCapacity = static_cast<uint32_t>(MAX_LIGHTS - MAX_ANALYTIC_LIGHTS);

    if (!state->debug.restir.bEmissiveTriangleLights) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Emissive Triangle Lights is OFF (Lighting > Direct Lighting (ReSTIR))");
    }
    ImGui::Text("Reserved instances    %u / %d meshes", emissive.reservedInstances, MAX_EMISSIVE_MESHES);
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("TriLightStore reservations held at fill time. Refused past the mesh cap."); }
    ImGui::Text("Meshlets              %u / %d", emissive.meshletWatermark, MAX_EMISSIVE_MESHLETS);
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Emissive meshlet slot watermark: one EmissiveMeshlet per LOD0 meshlet of every reserved instance. Past the cap an instance falls back to a single meshlet."); }
    ImGui::Text("Live meshes           %u", emissive.liveMeshes);
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Reservations that are live and visible, so lit on the GPU. A gap against reserved is visibility, not the store."); }
    ImGui::Text("Rebuilt this frame    %u meshes, %u triangles", emissive.rebuiltMeshes, emissive.rebuiltTriangles);
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Work items handed to the build pass, one workgroup each: only meshes dirty for this host slot. A quiet scene rebuilds nothing."); }
    ImGui::Text("TriLightStore         %u / %u (%.1f%%)", emissive.triLightWatermark, triLightCapacity,
                triLightCapacity > 0u ? 100.0f * static_cast<float>(emissive.triLightWatermark) / static_cast<float>(triLightCapacity) : 0.0f);
    ImGui::Text("Analytic lights       %u / %d", emissive.analyticLightCount, MAX_ANALYTIC_LIGHTS);
    const uint32_t lightCountFed = emissive.triLightCountFed > 0u ? static_cast<uint32_t>(MAX_ANALYTIC_LIGHTS) + emissive.triLightCountFed : emissive.analyticLightCount;
    ImGui::Text("LightData.lightCount  %u / %d", lightCountFed, MAX_LIGHTS);
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("What the GPU is told. Triangles live at [MAX_ANALYTIC_LIGHTS, lightCount); the gap below holds nothing."); }

    if (emissive.reservedInstances > emissive.liveMeshes) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%u reserved instance(s) not lit", emissive.reservedInstances - emissive.liveMeshes);
    }

    ImGui::Checkbox("Capture Per-Instance List", &emissive.bCapture);
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Every instance holding a reservation, lit or not, with the values the build pass reads."); }
    if (!emissive.bCapture) { return; }

    static bool bOnlyProblems = false;
    ImGui::Checkbox("Only Non-Live", &bOnlyProblems);
    if (emissive.bEntriesTruncated) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "list truncated at %u entries", Engine::EmissiveDebugState::MAX_ENTRIES);
    }

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##emissive_entries", 7, tableFlags, ImVec2(0.0f, 320.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Entity");
        ImGui::TableSetupColumn("Slot");
        ImGui::TableSetupColumn("Base");
        ImGui::TableSetupColumn("Tris");
        ImGui::TableSetupColumn("Mat");
        ImGui::TableSetupColumn("Emissive");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();

        for (const Engine::EmissiveDebugEntry& entry : emissive.entries) {
            const bool bLive = entry.dispatchState == Engine::EmissiveDispatchState::Live;
            if (bOnlyProblems && bLive) { continue; }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const auto* nameComponent = state->registry.try_get<Component::NameComponent>(entry.entity);
            ImGui::TextUnformatted(nameComponent ? nameComponent->name.c_str() : "<unnamed>");
            ImGui::TableNextColumn();
            ImGui::Text("%u", entry.instanceSlot);
            ImGui::TableNextColumn();
            ImGui::Text("%u", entry.firstLight);
            ImGui::TableNextColumn();
            ImGui::Text("%u", entry.triangleCount);
            ImGui::TableNextColumn();
            ImGui::Text("%u", entry.materialIndex);
            ImGui::TableNextColumn();
            // The build pass writes w * max(rgb) as the intensity, so show the product rather than the fields.
            const float maxChannel = glm::max(entry.emissiveFactor.x, glm::max(entry.emissiveFactor.y, entry.emissiveFactor.z));
            const float intensity = entry.emissiveFactor.w * maxChannel;
            if (intensity <= 0.0f) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%.2f %.2f %.2f x%.1f = 0", entry.emissiveFactor.x, entry.emissiveFactor.y, entry.emissiveFactor.z, entry.emissiveFactor.w);
            }
            else {
                ImGui::Text("%.2f %.2f %.2f x%.1f = %.2f", entry.emissiveFactor.x, entry.emissiveFactor.y, entry.emissiveFactor.z, entry.emissiveFactor.w, intensity);
            }
            ImGui::TableNextColumn();
            if (bLive) { ImGui::TextUnformatted("live"); }
            else { ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", EmissiveDispatchStateName(entry.dispatchState)); }
        }
        ImGui::EndTable();
    }
}

static void DrawSearchBar(ImGuiTextFilter& filter, const char* id)
{
    const float clearWidth = ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(-(clearWidth + ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::InputTextWithHint("##filter", "Search groups or fields", filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf))) {
        filter.Build();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        filter.Clear();
    }
    ImGui::PopID();
}

static void SetDebugViewTarget(Engine::EngineState* state, const char* resourceName, bool bEnabled)
{
    if (bEnabled) {
        state->debug.resourceName = Core::InlineString(resourceName);
        state->debug.transformationType = DebugTransformationType::None;
        state->debug.viewAspect = Core::DebugViewAspect::None;
    }
    else if (state->debug.resourceName == resourceName) {
        state->debug.resourceName.Clear();
    }
}

static void DebugViewButton(Engine::EngineState* state, const char* label, const char* resourceName, DebugTransformationType transform, Core::DebugViewAspect aspect)
{
    const bool bActive = state->debug.resourceName == resourceName && state->debug.transformationType == transform && state->debug.viewAspect == aspect;
    if (!Widgets::ToggleButton(label, bActive)) { return; }
    if (bActive) {
        state->debug.resourceName.Clear();
    }
    else {
        state->debug.resourceName = Core::InlineString(resourceName);
        state->debug.transformationType = transform;
        state->debug.viewAspect = aspect;
    }
}

void DrawDebugViewWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (ImGui::Begin("Debug View")) {
        Core::DebugRenderParams& render = state->debug.render;
        const Core::ReSTIRParams& restir = state->debug.restir;
        const bool bReSTIRMode = state->lighting.lightingMode == Core::LightingMode::ReSTIR;
        const bool bSigmaActive = state->lighting.lightingMode == Core::LightingMode::Default || (bReSTIRMode && !restir.bSunLight);
        const Core::AntiAliasingMode aaMode = state->lighting.aaConfig.mode;

        auto view = [&](const char* label, const char* resourceName, DebugTransformationType transform = DebugTransformationType::None, Core::DebugViewAspect aspect = Core::DebugViewAspect::None) {
            DebugViewButton(state, label, resourceName, transform, aspect);
        };
        auto depthView = [&](const char* label, DebugTransformationType transform) {
            DebugViewButton(state, label, "depth_target", transform, Core::DebugViewAspect::Depth);
        };

        ImGui::Checkbox("Enable UI", &state->debug.bEnableUI);
        ImGui::SameLine();
        ImGui::Checkbox("Wireframe", &render.bWireframe);

        ImGui::Text("View: %s", state->debug.resourceName.IsEmpty() ? "None" : state->debug.resourceName.c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(state->debug.resourceName.IsEmpty());
        if (ImGui::Button("Disable##debugview")) {
            state->debug.resourceName.Clear();
        }
        ImGui::EndDisabled();

        static ImGuiTextFilter debugViewFilter;
        DrawSearchBar(debugViewFilter, "debugviewfilter");
        ImGui::Separator();

        Widgets::BeginFilter(&debugViewFilter);

        if (Widgets::BeginSection("Overlays")) {
            Widgets::Checkbox("Enable GPU Debug Draw", &render.bEnableGPUDebug);
            Widgets::SameLine();
            Widgets::Checkbox("Lock##GPUDebug", &render.bLockGPUDebug);

            if (Widgets::Checkbox("Cluster Grid##GPUDebug", &render.bClusterGridDebug,
                                  "FrustumBinning: screen-frustum-tied clusters. Only actually bound in ReSTIR mode (feeds the reflection pass); Default-mode shading uses World Grid instead.") && render.bClusterGridDebug) {
                render.bWorldGridDebug = false;
            }
            Widgets::SameLine();
            if (Widgets::Checkbox("World Grid##GPUDebug", &render.bWorldGridDebug, "WorldGridBinning: camera-centered cascaded world-space grid used by Default-mode shading.") && render.bWorldGridDebug) {
                render.bClusterGridDebug = false;
            }
            if (Widgets::PassFilter("World Grid Level")) {
                const char* worldGridLevelLabels[] = {"All", "0", "1", "2", "3", "4", "5", "6", "7"};
                int worldGridLevelChoice = render.worldGridDebugLevel + 1;
                Widgets::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                if (Widgets::Combo("Level##WorldGridDebug", &worldGridLevelChoice, worldGridLevelLabels, static_cast<int>(std::size(worldGridLevelLabels)),
                                   "All draws every cascade with an identification tint; picking one cascade draws only it, untinted. 0 is the finest (32m) cascade, doubling per level.")) {
                    render.worldGridDebugLevel = worldGridLevelChoice - 1;
                }
            }

            Widgets::Checkbox("Probe Preview##GPUDebug", &state->debug.bProbePreview,
                              "Draws a sphere at every reflection probe's capture position, shaded from its cubemap (disabled probes included if their content is loaded). Specular samples the roughness-selected prefilter mip along the mirror reflection vector; Irradiance samples the diffuse mip along the normal.");
            Widgets::SameLine();
            Widgets::Checkbox("Irradiance##ProbePreview", &state->debug.bProbePreviewIrradiance);
            if (Widgets::PassFilter("Probe Preview Roughness")) {
                Widgets::SameLine();
                ImGui::SetNextItemWidth(200.0f);
                ImGui::BeginDisabled(state->debug.bProbePreviewIrradiance);
                Widgets::SliderFloat("Roughness##ProbePreview", &state->debug.probePreviewRoughness, 0.0f, 1.0f, {.format = "%.2f", .tooltip = "Roughness whose prefilter mip the preview sphere displays; matches the mapping shading uses.", .reset = true, .resetTo = 0.0});
                ImGui::EndDisabled();
            }

            Widgets::Checkbox("Radiance Cache##GPUDebug", &render.bRadianceCacheDebug,
                              "Radiance cache: one solid cube per occupied hash-table cell, colored by decoded radiance (black = occupied but not yet shaded). Cube size follows the cell's LOD; shrunk slightly so neighbors don't merge.");
            if (Widgets::PassFilter("Radiance Cache Exposure")) {
                Widgets::SameLine();
                ImGui::SetNextItemWidth(200.0f);
                Widgets::SliderFloat("Cache Exposure##GPUDebug", &render.radianceCacheDebugExposure, 0.1f, 10.0f,
                                     {.format = "%.2f", .tooltip = "Linear exposure applied only to the radiance cache debug cubes so bright cells do not blow out to flat white. Visualization only; does not affect lighting.", .reset = true, .resetTo = 1.0});
            }
            if (Widgets::PassFilter("Radiance Cache Direction")) {
                const char* bucketLabels[] = {"All", "+X", "-X", "+Y", "-Y", "+Z", "-Z"};
                int bucketChoice = render.radianceCacheDebugBucket + 1;
                Widgets::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                if (Widgets::Combo("Direction##RadianceCacheDebug", &bucketChoice, bucketLabels, static_cast<int>(std::size(bucketLabels)),
                                   "All draws every normal bucket; picking one draws only cells whose normal bucket matches (front/back separation only, not fine direction).")) {
                    render.radianceCacheDebugBucket = bucketChoice - 1;
                }
            }

            Widgets::SubHeader("DDGI Probes");
            Widgets::Checkbox("Draw Probes##DDGIDebug", &render.bDDGIProbeDebug);
            Widgets::SameLine();
            Widgets::Checkbox("Bounce Only##DDGIDebug", &render.bDDGIBounceOnly,
                              "Zero skybox radiance in the DDGI trace (feedback also disabled); anything left in the probes is one-bounce surface shading (sun + emissive + light-proxy emission at hits)");
            Widgets::SameLine();
            Widgets::Checkbox("Hide Inactive##DDGIDebug", &render.bDDGIHideInactiveProbes, "Skip classification-inactive probes in the debug view instead of drawing them flat blue.");

            const char* cascadeLabels[] = {"All", "Locals", "0", "1", "2", "3"};
            int cascadeOptionCount = static_cast<int>(state->lighting.ddgi.cascadeCount) + 2;
            if (cascadeOptionCount < 3) { cascadeOptionCount = 3; }
            if (cascadeOptionCount > 6) { cascadeOptionCount = 6; }
            if (render.ddgiProbeDebugCascade + 2 >= cascadeOptionCount) {
                render.ddgiProbeDebugCascade = -1;
            }
            if (Widgets::PassFilter("Probe Cascade")) {
                int cascadeChoice = render.ddgiProbeDebugCascade == Render::DDGI_PROBE_DEBUG_LOCALS_ONLY ? 1 : render.ddgiProbeDebugCascade + 2;
                if (render.ddgiProbeDebugCascade == -1) { cascadeChoice = 0; }
                ImGui::SetNextItemWidth(80.0f);
                if (Widgets::Combo("Cascade##DDGIDebug", &cascadeChoice, cascadeLabels, cascadeOptionCount,
                                   "All draws every entry with an identification tint (cascade 0 white, 1 red, 2 green, 3 blue; locals continue the palette). Locals draws only the resident hand-placed volumes, tinted. Picking one cascade draws only it, untinted.")) {
                    render.ddgiProbeDebugCascade = cascadeChoice == 0 ? -1 : cascadeChoice == 1 ? Render::DDGI_PROBE_DEBUG_LOCALS_ONLY : cascadeChoice - 2;
                }
                Widgets::SameLine();
            }
            if (Widgets::PassFilter("Probe Exposure")) {
                ImGui::SetNextItemWidth(240.0f);
                Widgets::SliderFloat("Probe Exposure##GPUDebug", &render.ddgiProbeDebugExposure, 0.1f, 10.0f,
                                     {.format = "%.2f", .tooltip = "Linear exposure applied only to the DDGI probe debug spheres so bright probes do not blow out to flat white. Visualization only; does not affect lighting.", .reset = true, .resetTo = 1.0});
            }
            if (Widgets::PassFilter("Probe Display")) {
                const char* probeDisplayLabels[] = {"Irradiance", "Visibility", "Placement", "Age"};
                ImGui::SetNextItemWidth(120.0f);
                Widgets::Combo("Display##DDGIDebug", &render.ddgiProbeDebugMode, probeDisplayLabels, static_cast<int>(std::size(probeDisplayLabels)),
                               "Visibility shows the probe's distance atlas as L1 lobes: red = mean distance relative to the miss clamp per direction, green = std/mean. A red-hot lobe pointing into the room from an exterior probe is a miss-inflated mean, which bypasses the Chebyshev occlusion test at sampling. Placement drops the shading entirely and draws flat per-volume tint, for reading probe positions and window coverage; dead (red) and inactive (blue) probes still show through. Age tints each world volume by its update count: green ramp while warming (0-16), then black to white as it ages toward the cap; camera cascades always show full age.");
            }
            Widgets::EndSection();
        }

        if (Widgets::BeginSection("G-Buffer")) {
            Widgets::SubHeader("Visibility Buffer");
            view("Instance##visbuffer", "visibility_target", DebugTransformationType::VisBuffInstance);
            Widgets::SameLine();
            view("Meshlet##visbuffer", "visibility_target", DebugTransformationType::VisBuffMeshlet);
            Widgets::SameLine();
            view("Triangle##visbuffer", "visibility_target", DebugTransformationType::VisBuffTriangle);

            auto bucketView = [&](const char* label, Core::BucketDebugMode mode, const char* tooltip = nullptr) {
                const bool bActive = render.bucketDebugMode == mode && state->debug.resourceName == "bucket_debug_target";
                if (!Widgets::ToggleButton(label, bActive, tooltip)) { return; }
                render.bucketDebugMode = bActive ? Core::BucketDebugMode::Off : mode;
                SetDebugViewTarget(state, "bucket_debug_target", !bActive);
            };
            constexpr const char* BUCKET_TOOLTIP = "Tiles: fill = the pixel's own bucket (dim), one bright ring per bucket dispatched to the tile, outermost = lowest index; a ring hue with no matching fill is over-dispatch. Heat: tile color by bucket count, black 0, blue 1, cyan 2, green 3, yellow 4, orange 5, red 6, magenta 7+.";
            bucketView("Bucket Tiles (Shading)", Core::BucketDebugMode::ShadeBuckets, BUCKET_TOOLTIP);
            Widgets::SameLine();
            bucketView("Bucket Heat (Shading)", Core::BucketDebugMode::ShadeHeat, BUCKET_TOOLTIP);
            bucketView("Bucket Tiles (Lighting)", Core::BucketDebugMode::LightBuckets, BUCKET_TOOLTIP);
            Widgets::SameLine();
            bucketView("Bucket Heat (Lighting)", Core::BucketDebugMode::LightHeat, BUCKET_TOOLTIP);

            Widgets::SubHeader("Surface");
            depthView("Depth", DebugTransformationType::DepthRemap);
            Widgets::SameLine();
            view("Stencil", "depth_target", DebugTransformationType::StencilRemap, Core::DebugViewAspect::Stencil);
            Widgets::SameLine();
            depthView("View Space Position", DebugTransformationType::ViewSpacePosition);
            view("Albedo", "gbuffer_two", DebugTransformationType::GBufferAlbedo);
            Widgets::SameLine();
            view("Normal", "gbuffer_one", DebugTransformationType::GBufferNormal);
            Widgets::SameLine();
            view("PBR", "gbuffer_one", DebugTransformationType::GBufferPBR);
            Widgets::SameLine();
            view("Emissive", "gbuffer_two", DebugTransformationType::GBufferEmissive);
            view("Motion Vectors", "gbuffer_one", DebugTransformationType::GBufferMotionVectors);
            Widgets::SameLine();
            view("View Z Delta", "gbuffer_one", DebugTransformationType::GBufferViewZDelta);
            Widgets::SameLine();
            view("NdotV", "gbuffer_one", DebugTransformationType::NdotV);

            Widgets::SubHeader("Shading");
            view("Shading Output", "shading_output");
            view("Intermediate One (Diffuse)", "intermediate_one");
            Widgets::SameLine();
            view("Intermediate Two (Specular)", "intermediate_two");
            Widgets::EndSection();
        }

        if (bReSTIRMode && Widgets::BeginSection("Direct Lighting (ReSTIR)")) {
            Widgets::SubHeader("Reservoirs");
            depthView("Generate Light Index", DebugTransformationType::ReservoirLightIdx);
            Widgets::SameLine();
            depthView("Generate W", DebugTransformationType::ReservoirGenerateW);
            depthView("Temporal Light Index", DebugTransformationType::ReservoirTemporalLightIdx);
            Widgets::SameLine();
            depthView("Temporal W", DebugTransformationType::ReservoirTemporalW);
            depthView("Spatial Light Index", DebugTransformationType::ReservoirSpatialLightIdx);
            Widgets::SameLine();
            depthView("Spatial W", DebugTransformationType::ReservoirSpatialW);
            depthView("History Light Index", DebugTransformationType::ReservoirHistoryLightIdx);
            Widgets::SameLine();
            depthView("History W", DebugTransformationType::ReservoirHistoryW);

            if (restir.lightProposal == Core::ReSTIRParams::LightProposal::ReGIR) {
                Widgets::SubHeader("ReGIR");
                depthView("ReGIR Cell (hue) / Entry Count (brightness)", DebugTransformationType::ReGIRCell);
                depthView("ReGIR Cell Total Mass (log)", DebugTransformationType::ReGIRCellMass);
                depthView("ReGIR Cursor Cell", DebugTransformationType::ReGIRCursorCell);
            }

            Widgets::SubHeader("Confidence");
            view("History Confidence", "restir_confidence");
            Widgets::SameLine();
            view("Gradient", "restir_gradient");
            view("Signal", "restir_signal");
            Widgets::SameLine();
            view("Shadow Vis", "restir_shadow_vis");
            Widgets::SameLine();
            view("Sun Flip", "restir_sun_flip");
            Widgets::EndSection();
        }

        if (bSigmaActive && Widgets::BeginSection("Sun Shadow (SIGMA)")) {
            view("Trace Visibility", "rt_sun_shadow", DebugTransformationType::SunShadowVisibility);
            Widgets::SameLine();
            view("Trace Penumbra", "rt_sun_shadow", DebugTransformationType::SunShadowPenumbra);
            view("Tiles (Classify)", "sigma_tiles", DebugTransformationType::SunShadowTiles);
            Widgets::SameLine();
            view("Tiles (Smoothed)", "sigma_tiles_smoothed", DebugTransformationType::SunShadowTileValue);
            view("Blur Visibility", "sigma_shadow", DebugTransformationType::SunShadowVisibility);
            Widgets::SameLine();
            view("Blur Penumbra", "sigma_shadow", DebugTransformationType::SunShadowPenumbra);
            Widgets::SameLine();
            view("Post-Blur Visibility", "sigma_shadow_2", DebugTransformationType::SunShadowVisibility);
            view("Stabilized Visibility", "sigma_stabilized", DebugTransformationType::SunShadowVisibility);
            Widgets::SameLine();
            view("Stabilized Penumbra", "sigma_stabilized", DebugTransformationType::SunShadowPenumbra);
            Widgets::EndSection();
        }

        if (bReSTIRMode && restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX && Widgets::BeginSection("Denoiser (RELAX)")) {
            view("Tiles##relax", "relax_tiles");
            Widgets::SameLine();
            view("History Length##relax", "relax_history_length");
            Widgets::SameLine();
            view("Reproj Confidence##relax", "relax_spec_reproj_confidence");
            Widgets::SameLine();
            view("Prev NR##relax", "relax_prev_nr");
            Widgets::SubHeader("Diffuse");
            view("Prepass##relaxdiff", "relax_diff_prepass");
            Widgets::SameLine();
            view("Illum##relaxdiff", "relax_diff_illum");
            Widgets::SameLine();
            view("Fast##relaxdiff", "relax_diff_fast");
            Widgets::SameLine();
            view("History##relaxdiff", "relax_diff_hist");
            Widgets::SameLine();
            view("ATrous 0##relaxdiff", "relax_atrous_diff_0");
            Widgets::SubHeader("Specular");
            view("Prepass##relaxspec", "relax_spec_prepass");
            Widgets::SameLine();
            view("Illum##relaxspec", "relax_spec_illum");
            Widgets::SameLine();
            view("Fast##relaxspec", "relax_spec_fast");
            Widgets::SameLine();
            view("History##relaxspec", "relax_spec_hist");
            Widgets::SameLine();
            view("ATrous 0##relaxspec", "relax_atrous_spec_0");
            Widgets::SameLine();
            view("Hit Dist##relaxspec", "relax_spec_hit_dist");
            Widgets::EndSection();
        }

        if (bReSTIRMode && restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::ReBLUR && Widgets::BeginSection("Denoiser (ReBLUR)")) {
            view("Frames (DATA1)", "reblur_data1");
            Widgets::SameLine();
            view("Carried Frames", "reblur_internal_data", DebugTransformationType::ReblurInternalData);
            Widgets::SameLine();
            view("Occlusion+VHA", "reblur_data2", DebugTransformationType::ReblurData2);
            Widgets::SubHeader("Diffuse");
            view("Packed##reblurdiff", "reblur_diff_packed", DebugTransformationType::YCoCgSignal);
            Widgets::SameLine();
            view("Accum##reblurdiff", "reblur_diff_accum", DebugTransformationType::YCoCgSignal);
            Widgets::SameLine();
            view("HistoryFix##reblurdiff", "reblur_diff_hfix", DebugTransformationType::YCoCgSignal);
            Widgets::SameLine();
            view("Blur##reblurdiff", "reblur_diff_blur", DebugTransformationType::YCoCgSignal);
            view("PostBlur##reblurdiff", "reblur_diff_hist", DebugTransformationType::YCoCgSignal);
            Widgets::SameLine();
            view("Fast##reblurdiff", "reblur_diff_fast_fixed");
            Widgets::SameLine();
            view("Luma Stab##reblurdiff", "reblur_diff_luma_stab");
            Widgets::SubHeader("Specular");
            view("Packed##reblurspec", "reblur_spec_packed", DebugTransformationType::YCoCgSignal);
            Widgets::SameLine();
            view("Accum##reblurspec", "reblur_spec_accum", DebugTransformationType::YCoCgSignal);
            Widgets::SameLine();
            view("HistoryFix##reblurspec", "reblur_spec_hfix", DebugTransformationType::YCoCgSignal);
            Widgets::SameLine();
            view("Blur##reblurspec", "reblur_spec_blur", DebugTransformationType::YCoCgSignal);
            view("PostBlur##reblurspec", "reblur_spec_hist", DebugTransformationType::YCoCgSignal);
            Widgets::SameLine();
            view("Fast##reblurspec", "reblur_spec_fast_fixed");
            Widgets::SameLine();
            view("Luma Stab##reblurspec", "reblur_spec_luma_stab");
            Widgets::SameLine();
            view("HitDist##reblurspec", "reblur_spec_hit_dist");
            Widgets::EndSection();
        }

        if (Widgets::BeginSection("Ambient Occlusion (GTAO)")) {
            view("Depth##gtao", "gtao_depth");
            Widgets::SameLine();
            view("Edges##gtao", "gtao_edges");
            Widgets::SameLine();
            view("Bent Normals##gtao", "gtao_bent_normals", DebugTransformationType::BentNormal);
            view("AO##gtao", "gtao_ao");
            Widgets::SameLine();
            view("Filtered##gtao", "gtao_filtered");
            Widgets::SameLine();
            view("Temporal##gtao", "gtao_temporal", DebugTransformationType::GTAOTemporalAO);
            Widgets::SameLine();
            view("Temporal Count##gtao", "gtao_temporal", DebugTransformationType::GTAOTemporalCount);
            Widgets::SameLine();
            view("Resolved##gtao", "shadows_resolve_target", DebugTransformationType::GTAOResolved);
            Widgets::EndSection();
        }

        if (Widgets::BeginSection("Diffuse GI")) {
            if (Widgets::PassFilter("Gather View")) {
                const char* giGatherDebugLabels[] = {"Off", "Irradiance", "Tiers", "Hit Distance", "Accumulation", "Escape"};
                ImGui::SetNextItemWidth(160.0f);
                if (Widgets::Combo("Gather View##GIGatherDebug", &render.giGatherDebugMode, giGatherDebugLabels, static_cast<int>(std::size(giGatherDebugLabels)),
                                   "Final-gather view in the debug visualizer; runs the gather even when it is not applied, and lighting/lit history stay live so the screen tier behaves as in normal play. Irradiance = upscaled gather evaluated at the pixel normal. Tiers = where the first ray resolved: cyan screen, green cache, blue probe, yellow sky, red backface, magenta baked probe. Hit Distance = hitT grayscale. Accumulation = temporal counter (white = full history). Escape = first-ray classification: yellow sky miss, magenta backface, front-face hit distance green to red over 2-10m; red/yellow/magenta at interior texels means the ray left the room.")) {
                    SetDebugViewTarget(state, "gi_gather_debug_target", render.giGatherDebugMode != 0);
                }
            }
            if (Widgets::PassFilter("Deconstruct View")) {
                const char* giDeconstructLabels[] = {"Off", "Cache Cell ID", "Cache Radiance", "DDGI Cheb Gate", "DDGI Mean vs Dist", "DDGI Coverage", "DDGI Irradiance", "World Volume Coverage", "DDGI Cascade"};
                ImGui::SetNextItemWidth(160.0f);
                if (Widgets::Combo("Deconstruct View##GIDeconstruct", &render.giDeconstructMode, giDeconstructLabels, static_cast<int>(std::size(giDeconstructLabels)),
                                   "Per-pixel GI leak deconstruction at the primary surface. Cache Cell ID = hash color of the resolved radiance-cache cell; the same color on both sides of a wall means interior and exterior share one cell. Cache Radiance = what gather tier 1 would return (magenta = found but not servable). DDGI Cheb Gate = weight fractions: red = occlusion test bypassed with a miss-inflated mean, green = bypassed with a plausible mean, blue = test ran. Mean vs Dist = dominant probe's (mean - distance)/spacing: red = bypassed, green = tested; blue overlays std/mean. Coverage = R coverage, G confidence, B serving cascade. Irradiance = raw DDGI injection at the pixel. World Volume Coverage = volume placement aid: green means an authored volume owns the surface, yellow is its fade band, red means cascades serve it (leaky at interior corners), magenta means nothing covers it; brightness tracks coverage so starved pockets read dark. DDGI Cascade = serving slot (cascade 0 red, 1 orange, 2 yellow, 3 green, 4 cyan, 5 blue, locals white), black stripes = not updated for 32 frames.")) {
                    SetDebugViewTarget(state, "gi_deconstruct_target", render.giDeconstructMode != 0);
                }
            }
            Widgets::EndSection();
        }

        if (Widgets::BeginSection("Reflections")) {
            view("Raw Traced (Demodulated)", "reflection_spec_noisy");
            Widgets::SubHeader("Reflection Probes");
            depthView("Reflection Probe Index", DebugTransformationType::ReflectionProbeIndex);
            Widgets::SameLine();
            depthView("Probe Bin Disagreement", DebugTransformationType::ReflectionProbeBinDisagreement);
            Widgets::EndSection();
        }

        if (Widgets::BeginSection("Culling")) {
            Widgets::Checkbox("Inst Frustum##Cull", &render.bCullInstanceFrustum);
            Widgets::SameLine();
            Widgets::Checkbox("Inst Contribution##Cull", &render.bCullInstanceContribution);
            Widgets::Checkbox("Mlet Frustum##Cull", &render.bCullMeshletFrustum);
            Widgets::SameLine();
            Widgets::Checkbox("Mlet Cone##Cull", &render.bCullMeshletCone);
            Widgets::SameLine();
            Widgets::Checkbox("Mlet Contribution##Cull", &render.bCullMeshletContribution);
            Widgets::Checkbox("Occlusion Culling##HiZ", &render.bOcclusionCulling,
                              "Two-phase Hi-Z occlusion culling of the visibility-buffer geometry pass. Phase 1 draws what was visible last frame, phase 2 re-tests everything against the fresh depth pyramid and late-draws disocclusions, so the final image is identical to no culling. Off = frustum-only.");
            Widgets::SameLine();
            Widgets::Checkbox("Freeze##HiZ", &render.bOcclusionFreeze,
                              "Locks the visibility bits and stops phase 2, so only the frozen visible set draws. Move the camera or flip wireframe to see the culled geometry as holes. Debug only; the image is intentionally wrong while frozen.");
            if (Widgets::PassFilter("Hi-Z Mip")) {
                Widgets::SameLine();
                int hizMip = render.hizDebugMip;
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::SliderInt("Hi-Z Mip##HiZDebug", &hizMip, -1, 11, hizMip < 0 ? "Off" : "%d")) {
                    render.hizDebugMip = hizMip;
                    SetDebugViewTarget(state, "hiz_debug_target", hizMip >= 0);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Shows the occlusion Hi-Z depth pyramid at the chosen mip, nearest-upscaled, grayscale = pow(depth, 0.25) so reversed-Z far reads dark. Mip 0 is pow2-down of half render resolution; each level is a conservative min (farthest) reduce. Sky is black; higher mips should only ever get darker.");
                }
            }
            Widgets::EndSection();
        }

        const bool bTAAViews = aaMode == Core::AntiAliasingMode::TAA || aaMode == Core::AntiAliasingMode::NaiveTAA || aaMode == Core::AntiAliasingMode::SMAAT2X;
        const bool bSMAAViews = aaMode == Core::AntiAliasingMode::SMAA || aaMode == Core::AntiAliasingMode::SMAAT2X;
        if ((bTAAViews || bSMAAViews) && Widgets::BeginSection("Anti-Aliasing")) {
            if (bTAAViews) {
                view("TAA Current", "taa_current");
                Widgets::SameLine();
                view("TAA Output", "taa_output");
            }
            if (bSMAAViews) {
                view("SMAA Edges", "smaa_edges");
                Widgets::SameLine();
                view("SMAA Blend Weights", "smaa_blend");
                Widgets::SameLine();
                view("SMAA Output", "smaa_output");
            }
            Widgets::EndSection();
        }

        if (Widgets::BeginSection("Post-Processing")) {
            Widgets::SubHeader("Bloom");
            view("Bloom Chain", "bloom_chain");
            Widgets::SubHeader("Depth of Field");
            view("Half-Res Color##dof", "dof_color_coc");
            Widgets::SameLine();
            view("Circle of Confusion##dof", "dof_color_coc", DebugTransformationType::DofCoc);
            Widgets::SameLine();
            view("Zones##dof", "dof_color_coc", DebugTransformationType::DofZones);
            view("Tiled Max##dof", "dof_tiled_max", DebugTransformationType::DofTileMax);
            Widgets::SameLine();
            view("Tiled Neighbor Max##dof", "dof_tiled_neighbor_max", DebugTransformationType::DofTileMax);
            view("Near Layer##dof", "dof_near");
            Widgets::SameLine();
            view("Near Coverage##dof", "dof_near", DebugTransformationType::AlphaOnly);
            Widgets::SameLine();
            view("Near Filtered##dof", "dof_near_filtered");
            view("Far Layer##dof", "dof_far");
            Widgets::SameLine();
            view("Far Filtered##dof", "dof_far_filtered");
            Widgets::SameLine();
            view("DoF Output##dof", "dof_output");
            Widgets::SubHeader("Motion Blur");
            view("Velocity##motionblur", "motion_blur_velocity");
            Widgets::SameLine();
            view("Tiled Max##motionblur", "motion_blur_tiled_max");
            Widgets::SameLine();
            view("Neighbor Max##motionblur", "motion_blur_tiled_neighbor_max");
            Widgets::SameLine();
            view("Motion Blur Output##motionblur", "motion_blur_output");
            Widgets::SubHeader("Output");
            view("Finalize Output", "tonemap_output");
            Widgets::SameLine();
            view("Post Process Output", "post_process_output");
            Widgets::SameLine();
            view("Screen Fade Output", "screen_fade_output");
            Widgets::EndSection();
        }

        if (Widgets::BeginSection("Hotkeys")) {
            if (Widgets::IsShowingAll()) {
                const char* keyNames[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
                for (size_t i = 0; i < std::size(DEBUG_HOTKEYS); ++i) {
                    ImGui::Text("%s: %s (%s)", keyNames[i], DEBUG_HOTKEYS[i].name, DEBUG_HOTKEYS[i].resourceName);
                }
            }
            Widgets::EndSection();
        }

        Widgets::EndFilter();
    }
    ImGui::End();
}

// Fixed height: a zone must never reflow the window while the mouse is over the viewport
static bool BeginDiagnosticZone(const char* label, bool* bEnabled, bool bAvailable, const char* unavailableReason, float rows)
{
    ImGui::PushID(label);
    ImGui::BeginDisabled(!bAvailable);
    ImGui::Checkbox(label, bEnabled);
    ImGui::EndDisabled();
    if (!bAvailable && unavailableReason != nullptr) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", unavailableReason);
    }
    if (!bAvailable || !*bEnabled) {
        ImGui::PopID();
        return false;
    }

    const float height = rows * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2.0f;
    const bool bVisible = ImGui::BeginChild("##zone", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
    if (!bVisible) {
        ImGui::EndChild();
        ImGui::PopID();
        return false;
    }
    return true;
}

static void EndDiagnosticZone()
{
    ImGui::EndChild();
    ImGui::PopID();
}

static void DrawLatchedMarker(bool bLatched)
{
    if (!bLatched) { return; }
    ImGui::SameLine();
    ImGui::TextDisabled("(latched)");
}

void DrawDiagnosticsWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    static Engine::ReGIRCursorCell latchedReGIRCursor{};
    static Engine::WorldGridCursorCell latchedWorldGridCursor{};

    if (ImGui::Begin("Diagnostics")) {
        Engine::DiagnosticsState& diagnostics = state->debug.diagnostics;
        const Core::ReSTIRParams& restir = state->debug.restir;
        const bool bReSTIRMode = state->lighting.lightingMode == Core::LightingMode::ReSTIR;
        const bool bReGIR = bReSTIRMode && restir.lightProposal == Core::ReSTIRParams::LightProposal::ReGIR;
        const bool bWorldGridBin = bReSTIRMode && restir.lightProposal == Core::ReSTIRParams::LightProposal::WorldGridBin;

        if (BeginDiagnosticZone("Radiance Cache Occupancy", &diagnostics.bRadianceCache, state->lighting.ddgi.bEnabled, "needs DDGI enabled", 6.0f)) {
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
            EndDiagnosticZone();
        }

        if (BeginDiagnosticZone("ReGIR Counters", &diagnostics.bReGIR, bReGIR, "needs ReSTIR + Light Proposal = ReGIR", 2.0f)) {
            ImGui::Text("Active cells: %u / %u, inserts failed/frame: %u, gather overflow/frame: %u",
                        ctx->regirStats.activeCells, REGIR_HASH_CAPACITY, ctx->regirStats.insertsFailed, ctx->regirStats.gatherOverflow);
            ImGui::Text("Cone rejected/frame: %u", ctx->regirStats.coneRejected);
            EndDiagnosticZone();
        }

        if (!diagnostics.bReGIRCursor) { latchedReGIRCursor = Engine::ReGIRCursorCell{}; }
        if (BeginDiagnosticZone("ReGIR Cursor Cell", &diagnostics.bReGIRCursor, bReGIR, "needs ReSTIR + Light Proposal = ReGIR", 9.0f)) {
            const Engine::ReGIRCursorCell& live = ctx->regirStats.cursor;
            if (live.valid != 0u) { latchedReGIRCursor = live; }
            const Engine::ReGIRCursorCell& cursor = latchedReGIRCursor;
            if (cursor.valid == 0u) {
                ImGui::TextDisabled("move the cursor over the viewport");
            }
            else {
                ImGui::Text("Cell L%u (%d, %d, %d) slot %u: %u / %u entries, total mass %.3g",
                            cursor.level, cursor.cell[0], cursor.cell[1], cursor.cell[2], cursor.slot, cursor.entryCount, REGIR_ENTRIES_PER_CELL, cursor.totalMass);
                DrawLatchedMarker(live.valid == 0u);
                for (uint32_t k = 0; k < 8; k++) {
                    if (cursor.topKey[k] == ~0u) { continue; }
                    const bool bMeshlet = (cursor.topKey[k] & REGIR_KEY_MESHLET) != 0u;
                    ImGui::Text("  %s %u, share %.2f%%, %u lights, at (%.1f, %.1f, %.1f)",
                                bMeshlet ? "meshlet" : "light", bMeshlet ? (cursor.topKey[k] & ~REGIR_KEY_MESHLET) : cursor.topKey[k],
                                cursor.topShare[k] * 100.0f, cursor.topLightCount[k], cursor.topPos[k * 3], cursor.topPos[k * 3 + 1], cursor.topPos[k * 3 + 2]);
                }
            }
            EndDiagnosticZone();
        }

        if (!diagnostics.bWorldGridCursor) { latchedWorldGridCursor = Engine::WorldGridCursorCell{}; }
        if (BeginDiagnosticZone("World Grid Cursor Cell", &diagnostics.bWorldGridCursor, bWorldGridBin, "needs ReSTIR + Light Proposal = WorldGridBin", 12.0f)) {
            const Engine::WorldGridCursorCell& live = ctx->worldGridCursor;
            if (live.valid != 0u) { latchedWorldGridCursor = live; }
            const Engine::WorldGridCursorCell& cursor = latchedWorldGridCursor;
            if (cursor.valid == 0u) {
                ImGui::TextDisabled("move the cursor over the viewport");
            }
            else {
                ImGui::Text("Cell L%u (%u, %u, %u) #%u, box (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f)", cursor.level, cursor.cell[0], cursor.cell[1], cursor.cell[2], cursor.flatIndex,
                            cursor.aabbMin[0], cursor.aabbMin[1], cursor.aabbMin[2], cursor.aabbMax[0], cursor.aabbMax[1], cursor.aabbMax[2]);
                DrawLatchedMarker(live.valid == 0u);
                ImGui::Text("Analytic: kept %u of %u in range (cap %u), kept power %.3g", cursor.analyticKept, cursor.analyticInRange, MAX_LIGHTS_PER_WORLD_GRID_CELL, cursor.analyticPower);
                for (uint32_t k = 0; k < 8; k++) {
                    if (cursor.topLightIdx[k] == ~0u) { continue; }
                    ImGui::Text("  light %u type %u, power %.3g, range %.1f, at (%.1f, %.1f, %.1f)", cursor.topLightIdx[k], cursor.topLightType[k], cursor.topLightPower[k], cursor.topLightRange[k],
                                cursor.topLightPos[k * 3], cursor.topLightPos[k * 3 + 1], cursor.topLightPos[k * 3 + 2]);
                }
                ImGui::Text("Emissive meshlets: kept %u of %u in range (cap %u), kept power %.3g", cursor.meshletKept, cursor.meshletInRange, MAX_EMISSIVE_MESHLETS_PER_WORLD_GRID_CELL, cursor.meshletPower);
                for (uint32_t k = 0; k < 8; k++) {
                    if (cursor.topMeshletIdx[k] == ~0u) { continue; }
                    ImGui::Text("  meshlet %u x%u tris, power %.3g, centre (%.1f, %.1f, %.1f)", cursor.topMeshletIdx[k], cursor.topMeshletLightCount[k], cursor.topMeshletPower[k],
                                cursor.topMeshletCenter[k * 3], cursor.topMeshletCenter[k * 3 + 1], cursor.topMeshletCenter[k * 3 + 2]);
                }
            }
            EndDiagnosticZone();
        }

        const bool bEmissiveAvailable = bReSTIRMode && restir.bEmissiveTriangleLights;
        const float emissiveRows = state->debug.emissive.bCapture ? 30.0f : 10.0f;
        if (BeginDiagnosticZone("Emissive Triangle Lights", &diagnostics.bEmissive, bEmissiveAvailable, "needs ReSTIR + Emissive Triangle Lights", emissiveRows)) {
            DrawEmissiveTriLightSection(state);
            EndDiagnosticZone();
        }

        if (BeginDiagnosticZone("Stores", &diagnostics.bStores, true, nullptr, 2.0f)) {
#ifdef WDEBUG
            if (ImGui::Button("Verify Dirty Stores")) {
                state->debug.bVerifyStoresOnce = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Runs for one frame: reports lights and instances that drifted from their store, and re-sends every live model and instance. Geometry twitching means a mutation skipped its dirty mark.");
            }
#else
            ImGui::TextDisabled("debug builds only");
#endif
            EndDiagnosticZone();
        }
    }
    ImGui::End();
}

static void DrawRELAXParamsUI(bool& changed, Core::RELAXParams& relax, const char* idScope, bool bShowChromaAtrous, bool bNrdMode = false)
{
    ImGui::PushID(idScope);

    static const Core::RELAXParams relaxDefaults{};
    auto relaxF = [&](const char* label, float* v, float def, float mn, float mx, const char* fmt = "%.4f", const char* tip = nullptr) {
        changed |= Widgets::SliderFloat(label, v, mn, mx, {.format = fmt, .tooltip = tip, .reset = true, .resetTo = def});
    };
    auto relaxI = [&](const char* label, int* v, int def, int mn, int mx, const char* tip = nullptr) {
        changed |= Widgets::SliderInt(label, v, mn, mx, {.tooltip = tip, .reset = true, .resetTo = static_cast<double>(def)});
    };

    changed |= Widgets::Checkbox("Prepass##relax", &relax.enablePrepass, "Spatial pre-blur before temporal accumulation, lowering the input noise fed into history. Default on.");
    Widgets::SameLine();
    changed |= Widgets::Checkbox("Anti-Firefly##relax", &relax.enableAntiFirefly, "Suppresses isolated bright outlier pixels (fireflies) before accumulation. Default on.");
    changed |= Widgets::Checkbox("Roughness Edge Stopping##relax", &relax.roughnessEdgeStoppingEnabled, "Roughness-aware specular edge stopping (roughness + oriented-normal weights). Off uses a simpler normal-only weight. Default on.");

    Widgets::SubHeader("General");
    ImGui::BeginDisabled(bNrdMode);
    relaxF("Denoising Range", &relax.denoisingRange, relaxDefaults.denoisingRange, 10.f, 5000.f, "%.1f", "Max view-space distance (world units) that gets denoised; farther surfaces pass through untouched. Default 1000; set to roughly cover your scene depth.");
    ImGui::EndDisabled();
    relaxF("Disocclusion Threshold", &relax.disocclusionThreshold, relaxDefaults.disocclusionThreshold, 0.001f, 0.05f, "%.4f", "Relative depth tolerance for accepting reprojected history. Higher accepts more (less ghosting rejection); lower resets more on edges/motion. A jitter/1px depth bonus is added on top. Default 0.01.");
    relaxF("Depth Threshold", &relax.depthThreshold, relaxDefaults.depthThreshold, 0.0f, 0.05f, "%.4f", "Plane-distance tolerance for spatial edge stopping, as a fraction of depth. Lower preserves geometry edges; higher blurs across them. Default 0.003.");

    Widgets::SubHeader("Accumulation");
    relaxF("Spec Max Accum Frames", &relax.specMaxAccumFrames, relaxDefaults.specMaxAccumFrames, 0.f, 64.f, "%.0f", "Max specular history length (stable). Higher = cleaner but laggier reflections. Frames at 60 fps, scaled with frame rate. Default 32; common 30-60.");
    relaxF("Spec Max Fast Accum Frames", &relax.specMaxFastAccumFrames, relaxDefaults.specMaxFastAccumFrames, 0.f, 16.f, "%.0f", "Length of the noisy 'fast' specular history used to clamp the slow one (anti-lag). Must be below Spec Max Accum to enable clamping. Frames at 60 fps, scaled with frame rate. NRD default 6 (~5x below main). Default 6.");
    relaxF("Diff Max Accum Frames", &relax.diffMaxAccumFrames, relaxDefaults.diffMaxAccumFrames, 0.f, 64.f, "%.0f", "Max diffuse history length (stable). Higher = cleaner but slower to react to lighting changes (more lag). Frames at 60 fps, scaled with frame rate. Default 32; common 30-60.");
    relaxF("Diff Max Fast Accum Frames", &relax.diffMaxFastAccumFrames, relaxDefaults.diffMaxFastAccumFrames, 0.f, 16.f, "%.0f", "Length of the noisy 'fast' diffuse history used to clamp the slow one (anti-lag). Lower = snappier response. Must be below Diff Max Accum. Frames at 60 fps, scaled with frame rate. NRD default 6. Default 6.");
    relaxF("History Acceleration Amount", &relax.historyAccelerationAmount, relaxDefaults.historyAccelerationAmount, 0.f, 1.f, "%.2f", "Strength of anti-lag acceleration pushing the slow history toward the fast one on changes. 0 = off, 1 = max. Default 1.0.");

    Widgets::SubHeader("Prepass");
    relaxF("Diff Blur Radius", &relax.diffBlurRadius, relaxDefaults.diffBlurRadius, 0.f, 100.f, "%.1f", "Radius (px) of the diffuse pre-blur applied before accumulation. Larger knocks down more input noise but loses detail. 0 disables. Default 30.");
    relaxF("Spec Blur Radius", &relax.specBlurRadius, relaxDefaults.specBlurRadius, 0.f, 100.f, "%.1f", "Radius (px) of the specular pre-blur before accumulation. 0 disables. Default 50.");
    relaxF("Min Hit Distance Weight", &relax.minHitDistanceWeight, relaxDefaults.minHitDistanceWeight, 0.f, 1.f, "%.2f", "Minimum weight for ray hit-distance when reconstructing specular in the prepass. 0 ignores hitT. Default 0; NRD commonly ~0.1-0.2.");

    Widgets::SubHeader("A-Trous / Edge Stopping");
    relaxI("ATrous Iterations", &relax.atrousIterations, relaxDefaults.atrousIterations, 2, 8, "Number of A-trous wavelet (spatial) passes; step doubles each pass (reach ~2^n px). More = wider denoising, costlier. NRD default 5. Default 5.");
    if (bShowChromaAtrous) {
        ImGui::BeginDisabled(bNrdMode);
        changed |= Widgets::Checkbox("Chroma Widening##relax", &relax.bChromaAtrous,
                                     "Extra diffuse-only passes filtering chroma (CoCg chromaticity) with geometric weights only; luminance untouched. Targets low-frequency hue blotches from spatially-reused light selection, which sit past the main chain's reach. Default on.");
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

    Widgets::SubHeader("History Fix");
    relaxF("Hist Fix Edge Stop Normal Pow", &relax.historyFixEdgeStoppingNormalPower, relaxDefaults.historyFixEdgeStoppingNormalPower, 0.f, 32.f, "%.1f", "Normal-match strictness for the history-fix fill that bootstraps fresh pixels. Higher = stricter normal matching. Default 8.");
    relaxF("Hist Fix Frame Num", &relax.historyFixFrameNum, relaxDefaults.historyFixFrameNum, 0.f, 32.f, "%.1f", "Pixels with history shorter than this get a sparse spatial fill (bootstrap) instead of relying on accumulation. 0 disables. Default 4.");
    relaxF("Hist Fix Base Pixel Stride", &relax.historyFixBasePixelStride, relaxDefaults.historyFixBasePixelStride, 0.f, 32.f, "%.1f", "Base sample spacing (px) for the history-fix fill; shrinks as history grows. Larger = wider initial fill. Default 14.");

    Widgets::SubHeader("History Clamp / Reset");
    relaxF("Fast History Clamp Sigma", &relax.fastHistoryClampingSigmaScale, relaxDefaults.fastHistoryClampingSigmaScale, 0.f, 8.f, "%.2f", "Width (in sigmas) of the fast-history color box that clamps the slow history (anti-lag/anti-ghosting). Lower = tighter clamp, less lag but more noise. Default 2.0; common 1-2.");
    relaxF("History Reset Temporal Sigma", &relax.historyResetTemporalSigmaScale, relaxDefaults.historyResetTemporalSigmaScale, 0.f, 10.f, "%.2f", "Temporal noise sigma scale in history-reset detection; larger tolerates more temporal noise before resetting. Default 5.");
    relaxF("History Reset Spatial Sigma", &relax.historyResetSpatialSigmaScale, relaxDefaults.historyResetSpatialSigmaScale, 0.f, 10.f, "%.2f", "Spatial noise sigma scale in history-reset detection; larger tolerates more spatial noise before resetting. Default 1.");
    relaxF("History Reset Amount", &relax.historyResetAmount, relaxDefaults.historyResetAmount, 0.f, 1.f, "%.2f", "How hard to snap history to the current noisy signal on big lighting changes. 0 = off (rely on clamping); 1 = aggressive. Default 0.5.");

    ImGui::Spacing();
    if (Widgets::Button("Reset RELAX")) {
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

    changed |= Widgets::Checkbox("Prepass##reblur", &reblur.enablePrepass);
    Widgets::SameLine();
    changed |= Widgets::Checkbox("Anti-Firefly##reblur", &reblur.enableAntiFirefly);
    changed |= Widgets::Checkbox("Temporal Stabilization##reblur", &reblur.enableTemporalStabilization);
    Widgets::SameLine();
    // No NRD counterpart (engine extension)
    ImGui::BeginDisabled(bNrdMode);
    changed |= Widgets::Checkbox("Stab. Firefly Cleanup##reblur", &reblur.enableStabilizationFireflyCleanup, "NRD short-history luma cap. Eats sparse disoccluded-pixel energy (black band on fast camera motion); keep OFF.");
    ImGui::EndDisabled();

    Widgets::SubHeader("General");
    ImGui::BeginDisabled(bNrdMode);
    reblurF("Denoising Range", &reblur.denoisingRange, reblurDefaults.denoisingRange, 10.f, 5000.f, "%.1f", "Max view-space distance (world units) that gets denoised; farther surfaces pass through. Default 1000.");
    ImGui::EndDisabled();
    reblurF("Disocclusion Threshold", &reblur.disocclusionThreshold, reblurDefaults.disocclusionThreshold, 0.001f, 0.05f, "%.4f", "Relative depth tolerance for accepting reprojected history. Default 0.01.");
    reblurF("Plane Distance Sensitivity", &reblur.planeDistanceSensitivity, reblurDefaults.planeDistanceSensitivity, 0.001f, 0.2f, "%.4f", "Max allowed deviation from the local tangent plane for spatial edge stopping. Default 0.02.");
    reblurF("Lobe Angle Fraction", &reblur.lobeAngleFraction, reblurDefaults.lobeAngleFraction, 0.f, 1.f, "%.3f", "Normal edge-stopping tolerance as a fraction of the BRDF lobe angle. Default 0.15.");
    reblurF("Roughness Fraction", &reblur.roughnessFraction, reblurDefaults.roughnessFraction, 0.f, 1.f, "%.3f", "Roughness edge-stopping tolerance (fraction). Default 0.15.");
    reblurF("Min Hit Distance Weight", &reblur.minHitDistanceWeight, reblurDefaults.minHitDistanceWeight, 0.f, 0.2f, "%.3f", "Sensitivity to hit distance in spatial passes; smaller for clean RTXDI-style hitT. Default 0.1.");

    Widgets::SubHeader("Hit Distance Normalization (A/B/C/D)");
    reblurF("Hit Dist A", &reblur.hitDistA, reblurDefaults.hitDistA, 0.f, 50.f, "%.2f", "Constant term (units). Default 3.");
    reblurF("Hit Dist B", &reblur.hitDistB, reblurDefaults.hitDistB, 0.f, 5.f, "%.3f", "viewZ-based linear scale. Default 0.1.");
    reblurF("Hit Dist C", &reblur.hitDistC, reblurDefaults.hitDistC, 1.f, 100.f, "%.1f", "Roughness-based scale (>1 = larger hit distance for low roughness). Default 20.");

    ImGui::BeginDisabled(bNrdMode);
    reblurF("Hit Dist D", &reblur.hitDistD, reblurDefaults.hitDistD, -50.f, 0.f, "%.1f", "Roughness falloff exponent (<=0). Default -25.");
    ImGui::EndDisabled();

    Widgets::SubHeader("Accumulation");
    reblurF("Max Accum Frames", &reblur.maxAccumulatedFrameNum, reblurDefaults.maxAccumulatedFrameNum, 0.f, 63.f, "%.0f", "Max (stable) history length. Higher = cleaner but laggier. Frames at 60 fps, scaled with frame rate. Default 30.");
    reblurF("Max Fast Accum Frames", &reblur.maxFastAccumulatedFrameNum, reblurDefaults.maxFastAccumulatedFrameNum, 0.f, 32.f, "%.0f", "Fast (responsive) history length used for anti-lag clamping. Usually ~1/6 of max. Frames at 60 fps, scaled with frame rate. Default 6.");
    reblurF("Max Stabilized Frames", &reblur.maxStabilizedFrameNum, reblurDefaults.maxStabilizedFrameNum, 0.f, 63.f, "%.0f", "History length for the temporal stabilization pass. 0 disables stabilization. Frames at 60 fps, scaled with frame rate. Default 30.");

    Widgets::SubHeader("Blur");
    reblurF("Min Blur Radius", &reblur.minBlurRadius, reblurDefaults.minBlurRadius, 0.f, 10.f, "%.2f", "Min denoising radius (px) for the converged state. Default 1.");
    reblurF("Max Blur Radius", &reblur.maxBlurRadius, reblurDefaults.maxBlurRadius, 0.f, 60.f, "%.1f", "Base (max) denoising radius (px); shrinks as history grows. Default 30.");
    reblurF("Diffuse Prepass Blur Radius", &reblur.diffusePrepassBlurRadius, reblurDefaults.diffusePrepassBlurRadius, 0.f, 100.f, "%.1f", "Diffuse pre-blur radius (px). 0 disables. Default 30.");
    reblurF("Specular Prepass Blur Radius", &reblur.specularPrepassBlurRadius, reblurDefaults.specularPrepassBlurRadius, 0.f, 100.f, "%.1f", "Specular pre-blur radius (px). 0 disables. Default 50.");

    ImGui::BeginDisabled(bNrdMode);
    changed |= Widgets::Checkbox("Chroma Widening##reblur", &reblur.bChromaAtrous,
                                 "Extra diffuse-only passes filtering chroma (CoCg chromaticity) with geometric weights only; luminance untouched. Targets low-frequency hue blotches from spatially-reused light selection, which sit past the main chain's reach. Default on.");
    reblurI("Chroma Widening Passes", &reblur.chromaAtrousIterations, reblurDefaults.chromaAtrousIterations, 1, 4, "Chroma pass count; strides 32/64/128/256, so each added pass doubles the hue-smoothing reach. Default 2.");
    reblurF("Chroma Luma Ratio Power", &reblur.chromaLumaPower, reblurDefaults.chromaLumaPower, 0.f, 6.f, "%.2f",
            "Falloff on the tap/center luminance ratio. Chroma taps carry no luminance and every other weight here is geometric, so without this a cast shadow (same plane, same normal) takes the lit side's hue as a colored halo. 0 = off. Integer values compile to a multiply chain. Default 2.");
    ImGui::EndDisabled();

    Widgets::SubHeader("History Fix");
    reblurF("Hist Fix Frame Num", &reblur.historyFixFrameNum, reblurDefaults.historyFixFrameNum, 0.f, 32.f, "%.1f", "Pixels with history shorter than this get a sparse spatial fill. Default 3.");
    reblurF("Hist Fix Base Pixel Stride", &reblur.historyFixBasePixelStride, reblurDefaults.historyFixBasePixelStride, 0.f, 32.f, "%.1f", "Base sample spacing (px) for the history-fix fill; shrinks as history grows. Default 14.");
    reblurF("Fast History Clamp Sigma", &reblur.fastHistoryClampingSigmaScale, reblurDefaults.fastHistoryClampingSigmaScale, 1.f, 3.f, "%.2f", "Width (sigmas) of the fast-history color box clamping the slow history. Default 2.");

    Widgets::SubHeader("Stabilization / Antilag");

    ImGui::BeginDisabled(bNrdMode);
    reblurF("Stabilization Strength", &reblur.stabilizationStrength, reblurDefaults.stabilizationStrength, 0.f, 1.f, "%.2f", "Blend toward the reprojected stabilized history. 0 = off. Default 1.");
    ImGui::EndDisabled();
    reblurF("Antilag Luminance Sigma", &reblur.antilagLuminanceSigmaScale, reblurDefaults.antilagLuminanceSigmaScale, 1.f, 5.f, "%.2f", "Color-box width (sigmas) used to clamp the stabilized history. Lower = tighter, less lag. Default 2.");
    reblurF("Firefly Suppressor Min Scale", &reblur.fireflySuppressorMinRelativeScale, reblurDefaults.fireflySuppressorMinRelativeScale, 1.f, 3.f, "%.2f", "Outlier suppression strength (smaller = stronger). Default 2.");

    ImGui::Spacing();
    if (Widgets::Button("Reset ReBLUR")) {
        reblur = Core::ReBLURParams{};
        changed = true;
    }
}

static void DrawProbeBakeSection(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (ImGui::Button("Bake All Probes##probebakeall")) {
        ProbeBakeGet(state).EnqueueAllProbes(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Bake All (2-Pass Interbounce)##probebakeall2")) {
        ProbeBakeGet(state).EnqueueAllProbesInterbounce(state);
    }

    ProbeBakeSystem* bake = &ProbeBakeGet(state);
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
}

void DrawLightingWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (ImGui::Begin("Lighting")) {
        bool changed = false;

        auto featureSection = [&](const char* label, bool* enabled, auto&& body) {
            if (!Widgets::IsShowingAll()) {
                if (Widgets::BeginSection(label)) {
                    if (Widgets::Checkbox(label, enabled)) { changed = true; }
                    ImGui::BeginDisabled(!*enabled);
                    body();
                    ImGui::EndDisabled();
                    Widgets::EndSection();
                }
                return;
            }
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

        if (ImGui::CollapsingHeader("Authoring")) {
            if (ImGui::Checkbox("Draw Reflection Probe Volumes##authoring", &state->lighting.reflectionProbe.bDebugDraw)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Every reflection probe volume with its capture marker; stale bakes in red. Face crosses on selected probes only.");
            }
            if (ImGui::Checkbox("Draw DDGI World Volumes##authoring", &state->lighting.ddgi.bDebugDrawVolumes)) { changed = true; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Every authored world volume as the region it fully owns (the probe window minus its one-cell fade band). Interior faces must sit inside the box. Face crosses on selected volumes only.");
            }
            if (Widgets::SliderFloat("Probe Line Width##authoring", &state->projectConfig.reflectionProbeLineWidth, 0.005f, 0.1f, {.format = "%.3f", .tooltip = "World-space width of reflection probe and DDGI world volume wireframes.", .reset = true, .resetTo = 0.02})) {
                Engine::WriteProjectConfig(state->projectConfig, state->allocator);
            }
            if (ImGui::CollapsingHeader("Probe Bake")) {
                DrawProbeBakeSection(ctx, state);
            }
        }

        ImGui::Separator();

        if (Widgets::SaveBar("lighting", &state->projectConfig.bAutoSaveLighting)) {
            SaveLightingTab(state);
        }
        DrawLightingProfiles(state);

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
                if (Widgets::ToggleButton(label, active)) {
                    state->lighting.groundTruthMode = active ? Core::GroundTruthMode::None : mode;
                    state->lighting.bResetGroundTruth = true;
                }
            };
            gtToggle("DI", Core::GroundTruthMode::DI);
            ImGui::SameLine();
            gtToggle("GI", Core::GroundTruthMode::GI);
            ImGui::SameLine();
            gtToggle("Full", Core::GroundTruthMode::Full);
            Widgets::SliderInt("GT Samples/Frame##gt", &state->lighting.groundTruthSpp, 1, 32,
                               {.tooltip = "Path-traced samples per pixel per frame; Full mode only, DI/GI stay 1. The probe bake overrides this with its own GT Samples/Frame while baking."});
            Widgets::SliderFloat("GT DoF Aperture##gt", &state->lighting.groundTruthDofAperture, 0.0f, 0.25f,
                                 {.tooltip = "Thin-lens aperture radius in meters; 0 = pinhole. Full mode only. Focus distance is shared with the Depth of Field settings. While active the raster DoF is skipped so the two never stack. A physical lens has one aperture: match the artist near/far radii one side at a time."});
        }

        ImGui::Separator();

        static ImGuiTextFilter lightingFilter;
        DrawSearchBar(lightingFilter, "lightingfilter");

        ImGui::Separator();

        const bool bIsGroundTruth = state->lighting.groundTruthMode != Core::GroundTruthMode::None;
        const bool bDefaultMode = state->lighting.lightingMode == Core::LightingMode::Default;
        const bool bReSTIRMode = state->lighting.lightingMode == Core::LightingMode::ReSTIR;

        RefreshLightingBaseline(state);
        const LightingBundle liveLighting = Engine::Profiles::CaptureLightingProfile(*state);

        Widgets::BeginFilter(&lightingFilter);

        Widgets::SectionHeader environmentHeader = MakeLightingSectionHeader(liveLighting, CopyEnvironmentSection);
        if (Widgets::BeginSection("Environment", &environmentHeader)) {
            if (Widgets::SliderFloat("IBL Intensity##env", &state->lighting.iblIntensity, 0.0f, 2.0f)) {
                changed = true;
            }
            if (Widgets::SliderFloat("Indirect Intensity##env", &state->lighting.indirectIntensity, 0.0f, 1.0f, {.tooltip = "Scales the indirect diffuse term at the final composite only (gather/probe/sky fill). Lower = darker shadows. Does not feed back into probe or cache convergence. Default 1.", .reset = true, .resetTo = 1.0})) {
                changed = true;
            }
            Widgets::EndSection();
        }
        HandleLightingSectionAction(state, environmentHeader, CopyEnvironmentSection);

        Widgets::SectionHeader directHeader = MakeLightingSectionHeader(liveLighting, CopyDirectLightingSection);
        if (bReSTIRMode && Widgets::BeginSection("Direct Lighting (ReSTIR)", &directHeader)) {
            Core::ReSTIRParams& restir = state->debug.restir;

            // Sections compiled out via restir_features_macros.h are greyed: the runtime toggle has no effect until the macro is set to 1 and shaders are rebuilt.
            Widgets::SubHeader("Candidate Generation");
            const char* proposalModes[] = {"World Grid Bin", "ReGIR"};
            int proposalIdx = static_cast<int>(restir.lightProposal);
            if (Widgets::Combo("Light Proposal", &proposalIdx, proposalModes, IM_ARRAYSIZE(proposalModes),
                               "Candidate source for ReSTIR DI. World Grid Bin: cascaded strongest-K analytic bin (sparse analytic scenes). ReGIR: per-cell deterministic entry table over a world hash grid (dense/emissive-triangle scenes).")) {
                restir.lightProposal = static_cast<Core::ReSTIRParams::LightProposal>(proposalIdx);
                changed = true;
            }
            ImGui::BeginDisabled(!RESTIR_ENABLE_INITIAL_VISIBILITY);
            if (Widgets::Checkbox("Initial Candidate Visibility", &restir.bInitialVisibility)) {
                changed = true;
            }
            ImGui::EndDisabled();
            featureSection("Emissive Triangle Lights", &restir.bEmissiveTriangleLights, [&] {
                if (Widgets::SliderFloat("Emissive Range Multiplier", &restir.emissiveTriRangeMultiplier, 0.0f, 0.25f,
                                         {.format = "%.4f", .tooltip = "Attenuation cutoff per emissive mesh, shared by all its triangles: range = multiplier * sqrt(intensity * total area). Raise if emissive fixtures darken with distance vs ground truth.", .reset = true, .resetTo = 0.03125f})) {
                    changed = true;
                }
            });

            Widgets::SubHeader("Temporal");
            if (Widgets::Checkbox("Temporal Reuse", &restir.bEnableTemporal)) {
                changed = true;
            }
            int temporalMCap = static_cast<int>(restir.temporalMCap);
            if (Widgets::SliderInt("Temporal M Cap", &temporalMCap, 1, 2000)) {
                restir.temporalMCap = static_cast<uint32_t>(temporalMCap);
                changed = true;
            }
            if (Widgets::Checkbox("Checkerboard Rendering", &restir.bCheckerboard)) {
                changed = true;
            }
            ImGui::BeginDisabled(!restir.bCheckerboard);
            if (Widgets::Checkbox("Full-Rate Resolve", &restir.bCheckerboardFullRateResolve,
                                  "Keeps the expensive local-light reservoir passes half-rate, but traces sun visibility full-rate and shades every pixel. Hole pixels borrow a depth-matched horizontal neighbor's local reservoir and re-shade it at their own surface. The denoisers then receive a full-rate signal with no checkerboard reconstruction.")) {
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!RESTIR_ENABLE_PERMUTATION_SAMPLING);
            if (Widgets::Checkbox("Permutation Sampling", &restir.bPermutationSampling)) {
                changed = true;
            }
            ImGui::EndDisabled();
            if (Widgets::SliderFloat("Boiling Filter (0=off)", &restir.boilingFilterStrength, 0.0f, 1.0f)) {
                changed = true;
            }
            ImGui::BeginDisabled(!RESTIR_ENABLE_ANTILAG);
            featureSection("Antilag", &restir.bEnableAntilag, [&] {
                if (Widgets::SliderFloat("Antilag Strength##restir", &restir.antilagStrength, 0.0f, 1.0f,
                                         {.format = "%.2f", .tooltip = "Shrinks carried temporal M where the shadow term flipped vs reprojected history, so moving shadows lose their ghost trail. May add noise in soft-shadow boundaries.", .reset = true, .resetTo = 0.5f})) {
                    changed = true;
                }
            });
            ImGui::EndDisabled();

            Widgets::SubHeader("Sun");
            if (Widgets::Checkbox("Sun Visibility Pass", &restir.bSunLight, "Directional sun as a 1spp cone-traced visibility pass, denoised by RELAX with the local lights. Off: SIGMA + directional composite path.")) {
                changed = true;
            }
            if (restir.bSunLight) {
                if (Widgets::Checkbox("Sun Alpha Test Cutout", &state->lighting.sigmaParams.bAlphaTest, "Sun visibility rays alpha-test cutout surfaces (foliage, fences) instead of treating them as solid.")) {
                    changed = true;
                }
                if (state->lighting.sigmaParams.bAlphaTest) {
                    if (Widgets::SliderFloat("Sun Alpha Test Max Distance", &restir.sunAlphaTestMaxDistance, 0.0f, 200.0f,
                                             {.format = "%.1f m", .tooltip = "Shading points beyond this view depth treat cutout surfaces as solid for the sun ray."})) {
                        changed = true;
                    }
                }
            }

            Widgets::SubHeader("Spatial Reuse");
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
            if (Widgets::SliderFloat("ReSTIR W Clamp (0=off)", &restir.restirWClamp, 0.0f, 0.01f, {.format = "%.6f"})) {
                changed = true;
            }
            Widgets::EndSection();
        }
        HandleLightingSectionAction(state, directHeader, CopyDirectLightingSection);

        Widgets::SectionHeader sigmaHeader = MakeLightingSectionHeader(liveLighting, nullptr);
        if ((bDefaultMode || (bReSTIRMode && !state->debug.restir.bSunLight)) && Widgets::BeginSection("Sun Shadow (SIGMA)", &sigmaHeader)) {
            Core::SIGMAParams& sigma = state->lighting.sigmaParams;
            static const Core::SIGMAParams sigmaDefaults{};

            if (Widgets::Checkbox("Half Res##sigma", &sigma.bHalfRes, "Trace + denoise the sun shadow at half resolution, then bilaterally upsample. Cuts the trace/temporal cost; softens contact shadows. Matches half-res ReSTIR.")) { changed = true; }
            if (Widgets::Checkbox("Alpha Test Cutout##sigma", &sigma.bAlphaTest, "Sun shadow rays alpha-test cutout surfaces (foliage, fences) instead of treating them as solid. Costs a texture fetch per cutout candidate along the ray.")) { changed = true; }
            if (Widgets::Checkbox("Post-Blur##sigma", &sigma.enablePostBlur, "Second decorrelated spatial pass after the main blur. The single largest quality lever; cleans residual penumbra noise. Default on.")) { changed = true; }

            auto sigmaF = [&](const char* label, float* v, float def, float mn, float mx, const char* fmt, const char* tip) {
                if (Widgets::SliderFloat(label, v, mn, mx, {.format = fmt, .tooltip = tip, .reset = true, .resetTo = def})) { changed = true; }
            };
            sigmaF("History Weight##sigma", &sigma.historyWeight, sigmaDefaults.historyWeight, 0.0f, 0.875f, "%.2f", "Temporal stabilization strength. Higher = steadier but laggier on moving shadows; lower = snappier but shimmerier. Saturates at 0.875 (SIGMA history cap). Default 0.8.");
            sigmaF("Max Kernel Pixels##sigma", &sigma.maxKernelPixels, sigmaDefaults.maxKernelPixels, 1.0f, 64.0f, "%.0f", "Cap on the penumbra blur radius (px). Bounds cost on very soft shadows. Default 32.");
            sigmaF("Penumbra Scale##sigma", &sigma.penumbraScale, sigmaDefaults.penumbraScale, 0.0f, 4.0f, "%.2f", "Artistic multiplier on the estimated penumbra. >1 softer, <1 sharper. Default 1.0.");

            if (Widgets::Button("Reset SIGMA")) {
                sigma = Core::SIGMAParams{};
                changed = true;
            }
            Widgets::EndSection();
        }

        Widgets::SectionHeader denoiserHeader = MakeLightingSectionHeader(liveLighting, CopyReSTIRDenoiserSection);
        if (bReSTIRMode && Widgets::BeginSection("ReSTIR Denoiser", &denoiserHeader)) {
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
            if (Widgets::Combo("Denoiser Mode##denoiser", &denoiserIdx, denoiserModes, IM_ARRAYSIZE(denoiserModes))) {
                restir.denoiserMode = DENOISER_MODE_ORDER[denoiserIdx];
                changed = true;
            }

            const bool bConfidenceDenoiser = restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX || restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::NRD || restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::NRDReBLUR;
            if (bConfidenceDenoiser) {
                ImGui::BeginDisabled(!RESTIR_ENABLE_CONFIDENCE);
                featureSection("Confidence (Moving-Shadow Antilag)", &restir.bEnableConfidence, [&] {
                    if (Widgets::SliderFloat("History Confidence##restir", &restir.confidenceStrength, 0.0f, 1.0f,
                                             {.format = "%.2f", .tooltip = "Moving-shadow antilag: temporal luminance-gradient confidence fed to RELAX (RTXDI-style). Master mix.", .reset = true, .resetTo = 0.75f})) {
                        changed = true;
                    }
                    if (Widgets::SliderFloat("Confidence Sensitivity##restir", &restir.confidenceSensitivity, 0.5f, 16.0f,
                                             {.format = "%.2f", .tooltip = "Gain on the flipped fraction of a stratum. Higher = collapses history on smaller lighting changes (more aggressive antilag, more noise). A static scene flips nothing, so high values are safe.", .reset = true, .resetTo = 8.0f})) {
                        changed = true;
                    }
                    if (Widgets::SliderFloat("Confidence Darkness Bias##restir", &restir.confidenceDarknessBias, 0.0f, 65536.0f,
                                             {.format = "%.1f", .tooltip = "Floor added to the gradient normalizer so dark-region noise does not produce a large relative gradient (false history collapse).", .reset = true, .resetTo = 655.36f})) {
                        changed = true;
                    }
                    if (Widgets::SliderFloat("Confidence History##restir", &restir.confidenceHistoryLength, 0.0f, 16.0f,
                                             {.format = "%.1f", .tooltip = "Frames the confidence temporal filter holds a dip. Drops fast, recovers slowly; gives ReSTIR time to re-converge before RELAX trusts history again. 0 = no temporal filter.", .reset = true, .resetTo = 4.0f})) {
                        changed = true;
                    }
                    int confidenceBlurRadius = static_cast<int>(restir.confidenceBlurRadius);
                    if (Widgets::SliderInt("Confidence Blur##restir", &confidenceBlurRadius, 0, 6,
                                           {.tooltip = "Gradient blur radius (in downsampled gradient texels). Wider = smoother penumbra confidence, less noise; too wide blurs the antilag region."})) {
                        restir.confidenceBlurRadius = static_cast<uint32_t>(confidenceBlurRadius);
                        changed = true;
                    }
                });
                ImGui::EndDisabled();
            }

            if (restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX) {
                DrawRELAXParamsUI(changed, restir.relax, "main_relax", true);
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
            Widgets::EndSection();
        }
        HandleLightingSectionAction(state, denoiserHeader, CopyReSTIRDenoiserSection);

        Widgets::SectionHeader gtaoHeader = MakeLightingSectionHeader(liveLighting, CopyGTAOSection);
        if (Widgets::BeginSection("Ambient Occlusion (GTAO)", &gtaoHeader)) {
            Core::GTAOConfiguration& gtao = state->lighting.gtaoConfig;
            static const Core::GTAOConfiguration gtaoDefaults{};

            if (Widgets::Checkbox("Enable GTAO", &gtao.bEnabled)) { changed = true; }

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
            gtaoF("Temporal Max Accum##gtao", &gtao.temporalMaxAccum, gtaoDefaults.temporalMaxAccum, 0.0f, 64.0f, "%.0f", "Frames of motion-reprojected AO history blended in shadows_resolve, separate from TAA. Higher = smoother, more ghosting on movers; 0 = off. Frames at 60 fps, scaled with frame rate. Default 16.");
            gtaoF("Temporal Clamp Scale##gtao", &gtao.temporalClampScale, gtaoDefaults.temporalClampScale, 0.0f, 4.0f, "%.2f", "Width of the 3x3 neighborhood box the AO history is pulled into. Lower = less ghosting behind movers, more residual noise; 0 = no clamp. Default 1.");

            ImGui::Spacing();
            if (Widgets::Button("Reset GTAO")) {
                gtao = Core::GTAOConfiguration{};
                changed = true;
            }
            Widgets::EndSection();
        }
        HandleLightingSectionAction(state, gtaoHeader, CopyGTAOSection);

        Widgets::SectionHeader ddgiHeader = MakeLightingSectionHeader(liveLighting, CopyDDGISection);
        if (Widgets::BeginSection("Diffuse GI (DDGI)", &ddgiHeader)) {
            Core::DDGIParams& ddgi = state->lighting.ddgi;
            static const Core::DDGIParams ddgiDefaults{};

            auto ddgiF = [&](const char* label, float* v, float def, float mn, float mx, const char* fmt, const char* tip) {
                if (Widgets::SliderFloat(label, v, mn, mx, {.format = fmt, .tooltip = tip, .reset = true, .resetTo = def})) { changed = true; }
            };
            auto ddgiI = [&](const char* label, int* v, int def, int mn, int mx, const char* tip) {
                if (Widgets::SliderInt(label, v, mn, mx, {.tooltip = tip, .reset = true, .resetTo = static_cast<double>(def)})) { changed = true; }
            };

            if (Widgets::Checkbox("Enabled##ddgi", &ddgi.bEnabled)) { changed = true; }
            Widgets::SameLine();
            if (Widgets::Checkbox("Apply To Lighting##ddgi", &ddgi.bApplyToLighting, "Use the probes as the indirect diffuse in lighting (replaces the skybox irradiance where the volume covers). Off = probes still update, for A/B and the debug viz.")) { changed = true; }

            Widgets::SubHeader("Gather");
            if (Widgets::Checkbox("GI Diffuse Gather##ddgi", &ddgi.bFinalGather,
                                  "TDA-style resolve: one cosine ray per half-res pixel takes last frame's lit screen at its hit as-is, else the radiance cache, else albedo * probe irradiance (skybox on miss), projected into 2-band SH, a-trous denoised, bilaterally upscaled and accumulated with a plain frame counter. Debug views live in the Debug View window.")) { changed = true; }
            if (ddgi.bFinalGather) {
                if (Widgets::Checkbox("Denoise##gigather", &ddgi.bFinalGatherDenoise, "Separable bilateral blur on the gather SH before compositing (normal/depth/hit-distance edge stopping). Off = raw 1spp signal, for A/B.")) { changed = true; }
                Widgets::SameLine();
                if (Widgets::Checkbox("Temporal##gigather", &ddgi.bFinalGatherTemporal, "Counter accumulation of the resolved gather across frames (up to 24). Off = this frame's result only; with Denoise also off the composite shows the raw gather.")) { changed = true; }
                Widgets::SameLine();
                if (Widgets::Checkbox("Quarter Res##gigather", &ddgi.bFinalGatherQuarterRes,
                                      "Gather at quarter render resolution instead of half: 1/4 the rays and denoise cost. Each gather texel covers 4x4 full-res pixels, so contact detail leans harder on the bilateral guides and history.")) { changed = true; }
                int gatherRaysPerPixel = static_cast<int>(ddgi.gatherRaysPerPixel);
                if (Widgets::SliderInt("Rays Per Pixel##gigather", &gatherRaysPerPixel, 1, static_cast<int>(Render::GI_GATHER_MAX_RAYS_PER_PIXEL), {
                                           .tooltip = "Gather rays per half-res pixel, uniform across the frame so cost stays flat and rays stay coherent. Relative noise falls as 1/sqrt(n), which is the only lever that reaches the bright-to-dark gradients near small light slits, where one ray finds the aperture too rarely for any reweighting to help. Trace cost is linear.", .reset = true,
                                           .resetTo = 1.0
                                       })) {
                    ddgi.gatherRaysPerPixel = static_cast<uint32_t>(gatherRaysPerPixel);
                    changed = true;
                }
            }

            Widgets::SubHeader("Volume");
            ddgiI("Probe Count X##ddgi", &ddgi.probeCountX, ddgiDefaults.probeCountX, 2, 32, "Probes along X. The volume is a camera-following rolling window; changing counts restarts probe history.");
            ddgiI("Probe Count Y##ddgi", &ddgi.probeCountY, ddgiDefaults.probeCountY, 2, 32, "Probes along Y (vertical).");
            ddgiI("Probe Count Z##ddgi", &ddgi.probeCountZ, ddgiDefaults.probeCountZ, 2, 32, "Probes along Z.");
            ddgiF("Probe Spacing##ddgi", &ddgi.probeSpacing, ddgiDefaults.probeSpacing, 0.25f, 8.0f, "%.2f", "World-space distance between probes (meters); coverage = count * spacing per axis. Changing it restarts probe history.");
            int cascadeCount = static_cast<int>(ddgi.cascadeCount);
            if (Widgets::SliderInt("Cascade Count##ddgi", &cascadeCount, 1, static_cast<int>(DDGI_MAX_CAMERA_CASCADES), {
                                       .tooltip = "Concentric volumes with identical counts; each doubles the previous spacing, so range doubles per cascade for linear memory. Cascade 0 updates every other frame, outer cascades round-robin on the frames between (flat trace cost).", .reset = true, .resetTo = static_cast<double>(ddgiDefaults.cascadeCount)
                                   })) {
                ddgi.cascadeCount = static_cast<uint32_t>(cascadeCount);
                changed = true;
            }
            ddgiF("Edge Blend Cells##ddgi", &ddgi.edgeBlendCells, ddgiDefaults.edgeBlendCells, 1.0f, 8.0f, "%.1f", "Width (in probe cells) of each cascade's edge fade into the next coarser cascade (and the outermost cascade's fade to skybox). Wider = softer, less visible cascade boundary; costs double-sampling in the band.");
            if (Widgets::Checkbox("Scale Biases Per Cascade##ddgi", &ddgi.bScaleBiasPerCascade,
                                  "Multiply normal/view bias by each cascade's spacing scale (RTXGI-style). Off = all cascades sample at the same world-space bias, which can reduce fine-vs-coarse disagreement at cascade boundaries.")) { changed = true; }
            if (Widgets::Checkbox("Local Volumes##ddgi", &ddgi.bLocalVolumes, "Hand-placed fine-spacing probe volumes (LocalDDGIVolumeComponent entities), sampled before cascade 0 where they cover. Nearest few volumes stay resident, one updates per frame.")) { changed = true; }
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

            Widgets::SubHeader("Trace");
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
            if (Widgets::Checkbox("Probe Classification##ddgi", &ddgi.bClassification,
                                  "Probes with no geometry within ~1.5 cells drop to 16 sentinel rays and freeze their atlas tiles (nothing within sampling reach reads them); a sentinel hit reactivates the probe with a temporal restart. Requires Relocation (classification rides that pass). Inactive probes draw flat blue in the probe debug view.")) { changed = true; }
            if (Widgets::Checkbox("Infinite Bounce##ddgi", &ddgi.bInfiniteBounce,
                                  "Ray hits also sample last frame's probe atlas, so light keeps bouncing (one extra bounce lands per frame, damped by hysteresis). Also gives area/sphere lights indirect, since probes see their proxies directly.")) { changed = true; }
            ddgiF("Bounce Intensity##ddgi", &ddgi.bounceIntensity, ddgiDefaults.bounceIntensity, 0.0f, 1.0f, "%.2f",
                  "Scales the DDGI feedback term fed back into the radiance cache / probes. This is the cache<->DDGI feedback loop, so <1 bounds the loop gain: keeps enclosed high-albedo scenes from saturating and self-lighting. 1 = physically full multi-bounce. Default 1.");
            ddgiF("Max Ray Radiance##ddgi", &ddgi.maxRayRadiance, ddgiDefaults.maxRayRadiance, 0.0f, 6553600.0f, "%.0f", "Firefly clamp: hit radiance above this (max channel) is scaled down before blending, taming NEE light-selection spikes and rare bright emissive hits. Dims indirect from very bright small sources. 0 = off. Default 1310720.");

            Widgets::SubHeader("Blend");
            if (Widgets::Button("Converge Now##ddgi",
                                "Temporarily drops hysteresis, maxes rays, and accelerates radiance-cache shading (interval + accumulation window) for ~70 frames so dark multi-bounce rooms converge quickly, then restores the values below. Retrigger to restart the schedule.")) {
                state->debug.bGIFreeze = false;
                DDGIConvergeBoostTrigger(state->ddgiConvergeBoost, ddgi);
            }
            if (state->ddgiConvergeBoost.bActive && Widgets::PassFilter("Converge Now")) {
                ImGui::SameLine();
                ImGui::Text("Converging... %d frames left", DDGI_CONVERGE_BOOST_FRAMES - state->ddgiConvergeBoost.frame);
            }
            ddgiF("Hysteresis##ddgi", &ddgi.hysteresis, ddgiDefaults.hysteresis, 0.0f, 0.995f, "%.3f", "Temporal history weight for irradiance (RTXGI parity). Probe updates are 1spp Monte Carlo, so the EMA carries most of the smoothing; the darkening fast path plus the min darkening step keep lights-off response quick. Higher = smoother but laggier; 0 = no history. Per update at 60 fps, re-derived for the frame rate. Default 0.97.");
            ddgiF("Visibility Hysteresis##ddgi", &ddgi.visibilityHysteresis, ddgiDefaults.visibilityHysteresis, 0.0f, 0.995f, "%.3f", "Temporal history weight for the distance/Chebyshev atlas. The radiance cache stabilizes radiance only; the visibility integrand still changes every frame with ray rotation, so this stays high. Per update at 60 fps, re-derived for the frame rate. Default 0.97.");
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
                                       .tooltip = "Running-mean window cap for cache cell radiance: each shade event blends 1/(count+1) up to this. Lower = faster response, more variance. Shade events at 60 fps, scaled up with frame rate, never below this. Default 8.", .reset = true, .resetTo = static_cast<double>(ddgiDefaults.radianceCacheAccumCap)
                                   })) {
                ddgi.radianceCacheAccumCap = static_cast<uint32_t>(cacheAccumCap);
                changed = true;
            }
            ddgiF("Irradiance Gamma##ddgi", &ddgi.irradianceGamma, ddgiDefaults.irradianceGamma, 1.0f, 10.0f, "%.1f", "Perceptual encoding exponent: the atlas stores pow(E, 1/gamma) and blends in that space, so rare bright rays (sky through a small opening) cannot pulse the average. 1 = linear. Default 5.");
            ddgiF("Irradiance Threshold##ddgi", &ddgi.irradianceThreshold, ddgiDefaults.irradianceThreshold, 0.0f, 1.0f, "%.2f", "Darkening, as a fraction of the previous value, that counts as a real lighting change (lights turning off; RTXGI): hysteresis drops by 0.75 so the probe re-converges fast. Default 0.90.");
            ddgiF("Brightness Threshold##ddgi", &ddgi.brightnessThreshold, ddgiDefaults.brightnessThreshold, 0.0f, 10.0f, "%.2f", "Per-update brightening, as a multiple of the previous value, above which the delta is scaled to 25% (firefly/pulse suppression). Default 2.00.");
            ddgiF("Distance Exponent##ddgi", &ddgi.distanceExponent, ddgiDefaults.distanceExponent, 1.0f, 100.0f, "%.0f", "Sharpness of the cosine lobe used when integrating ray distances into the visibility atlas; higher = tighter Chebyshev occlusion, more leak-proof but noisier. Default 50.");

            Widgets::SubHeader("Sampling");
            ddgiF("Normal Bias##ddgi", &ddgi.normalBias, ddgiDefaults.normalBias, 0.0f, 1.0f, "%.2f", "Meters the sample point is pushed along the surface normal before probe lookup; fights self-shadowing (dark stripes on walls). Default 0.10.");
            ddgiF("View Bias##ddgi", &ddgi.viewBias, ddgiDefaults.viewBias, 0.0f, 2.0f, "%.2f", "Meters the sample point is pushed toward the viewer before probe lookup; fights leaks through thin walls near the camera ray. Default 0.30.");

            Widgets::SubHeader("Relocation");
            if (Widgets::Checkbox("Relocation##ddgi", &ddgi.bRelocation,
                                  "Probes inside geometry step out through the nearest backface (capped at 45% of spacing); probes still buried past the cap are classified dead and skipped at sampling (drawn red in the probe debug view).")) { changed = true; }
            ddgiF("Min Frontface Distance##ddgi", &ddgi.minFrontfaceDistance, ddgiDefaults.minFrontfaceDistance, 0.0f, 1.0f, "%.2f", "Meters of clearance relocation keeps between a probe and nearby geometry; probes closer than this to a wall get nudged away from it. Default 0.30.");

            ImGui::Spacing();
            if (Widgets::Button("Reset DDGI")) {
                ddgi = Core::DDGIParams{};
                changed = true;
            }
            Widgets::EndSection();
        }
        HandleLightingSectionAction(state, ddgiHeader, CopyDDGISection);

        Widgets::SectionHeader reflectionsHeader = MakeLightingSectionHeader(liveLighting, CopyReflectionsSection);
        if (Widgets::BeginSection("Reflections", &reflectionsHeader)) {
            Core::ReflectionConfiguration& reflection = state->lighting.reflection;
            static const Core::ReflectionConfiguration reflectionDefaults{};

            if (Widgets::Checkbox("Enable Reflections", &reflection.bEnabled)) { changed = true; }
            if (bReSTIRMode) {
                if (Widgets::Checkbox("Merged Denoise", &reflection.bMergedDenoise,
                                      "Sum the traced reflection radiance into the main denoiser's specular channel at the lighting resolve, so one denoiser covers lights + sun + reflections. Off: the raw noisy shade output composites directly in remodulate.")) { changed = true; }
            }
            if (Widgets::Checkbox("Screen-Space Hit Lighting", &reflection.bScreenSpaceLighting,
                                  "Reproject the reflection hit into last frame's lit image and reuse that fully shadowed color; falls back to unshadowed analytic hit shading when the hit is off-screen or occluded.")) { changed = true; }
            if (bDefaultMode) {
                if (Widgets::Checkbox("Screen-Space Trace", &reflection.bScreenSpaceTrace,
                                      "March the reflection ray against the depth buffer instead of the TLAS. Off-screen and occluded rays fall back to reflection probes then the skybox.")) { changed = true; }
            }
            if (Widgets::Checkbox("Alpha Test Mirror Hits", &reflection.bAlphaTest,
                                  "At/below Mirror Roughness Max, alpha-test cutout surfaces the reflection ray hit and continue the ray through transparent texels. Also alpha-tests the sun and local shadow rays at analytic hits. Off: cutout reflects and shadows as solid.")) { changed = true; }

            static const char* reflectionSunModes[] = {"Shadow Ray", "Always Lit", "Always Unlit"};
            int reflectionSunMode = static_cast<int>(reflection.sunMode);
            if (Widgets::Combo("Hit Sun Mode##reflection", &reflectionSunMode, reflectionSunModes, IM_ARRAYSIZE(reflectionSunModes),
                               "Sun term when a reflection hit falls back to analytic shading (screen-space reuse missed). Shadow Ray traces sun visibility at the hit; Always Lit skips the ray and assumes visible; Always Unlit drops the sun entirely (indoor scenes).")) {
                reflection.sunMode = static_cast<Core::ReflectionConfiguration::SunMode>(reflectionSunMode);
                changed = true;
            }

            auto reflF = [&](const char* label, float* v, float def, float mn, float mx, const char* fmt, const char* tip) {
                if (Widgets::SliderFloat(label, v, mn, mx, {.format = fmt, .tooltip = tip, .reset = true, .resetTo = def})) { changed = true; }
            };

            reflF("Traced Roughness Max##reflection", &reflection.tracedRoughnessMax, reflectionDefaults.tracedRoughnessMax, 0.0f, 1.0f, "%.2f", "Surfaces rougher than this fall back to the prefiltered skybox reflection instead of being ray traced. Lower = only near-mirror surfaces get traced reflections, cheaper. Default 0.3.");
            reflF("Light Specular From Reflections Max##reflection", &reflection.lightSpecularFromReflectionsMax, reflectionDefaults.lightSpecularFromReflectionsMax, 0.0f, 1.0f, "%.2f",
                  "Roughness at/below which local-light specular is left to the reflection providers (probes/RT) instead of shaded analytically. 1.0 = providers own all specular; low = only near-mirror deferred. Default 0.3 (= traced max; DI owns rough spec, probe bakes hide light proxies to avoid double count).");
            reflF("Mirror Roughness Max##reflection", &reflection.mirrorRoughnessMax, reflectionDefaults.mirrorRoughnessMax, 0.0f, 0.3f, "%.3f", "At/below this roughness the reflection ray is the exact mirror direction instead of a GGX sample (no lobe-tail grain, no emitter fireflies) and the ReSTIR BRDF technique is skipped for the pixel. Default 0.08.");
            reflF("Intensity##reflection", &reflection.intensity, reflectionDefaults.intensity, 0.0f, 2.0f, "%.2f", "Multiplier on the traced reflection radiance before compositing. Default 1.0.");
            reflF("Max Ray Intensity##reflection", &reflection.maxRayIntensity, reflectionDefaults.maxRayIntensity, 0.0f, 65536000.0f, "%.0f", "Luminance clamp on a single reflection ray's radiance (before demodulation). Bounds what one emitter hit can inject into the denoiser; biased darker on bright emitters. 0 = off. Default 0.");
            if (bDefaultMode && reflection.bScreenSpaceTrace) {
                reflF("SSR Thickness##reflection", &reflection.ssrThickness, reflectionDefaults.ssrThickness, 0.05f, 2.0f, "%.2f", "View-space depth window (meters) behind a surface that still counts as a hit. Larger = fewer gaps but more over-reflection behind thin objects. Default 0.3.");
                if (Widgets::SliderInt("SSR Max Steps##reflection", &reflection.ssrMaxSteps, 16, 256, {.tooltip = "Maximum march steps per ray before giving up. Higher = longer reflections, higher cost. Default 64.", .reset = true, .resetTo = static_cast<double>(reflectionDefaults.ssrMaxSteps)})) { changed = true; }
            }
            if (Widgets::SliderInt("Hit Local Shadow Rays##reflection", &reflection.hitLocalShadowRays, 0, static_cast<int>(REFLECTION_HIT_SHADOW_RAYS_MAX), {.tooltip = "Analytic hit shading: shadow rays spent on the brightest local-light contributions at the hit (sun has its own ray via Hit Sun Mode). Remaining lights stay unshadowed. 0 = none. Default 1.", .reset = true, .resetTo = static_cast<double>(reflectionDefaults.hitLocalShadowRays)})) { changed = true; }
            reflF("Hit Texture LOD##reflection", &reflection.hitTextureLod, reflectionDefaults.hitTextureLod, 0.0f, 8.0f, "%.1f", "Analytic hit shading: fixed mip level for albedo/emissive/metal-rough sampling at the hit. 0 = full-res (sharper, more cache pressure). Default 3.");

            ImGui::Spacing();
            if (Widgets::Button("Reset RT Reflections")) {
                reflection = Core::ReflectionConfiguration{};
                changed = true;
            }
            Widgets::EndSection();
        }
        HandleLightingSectionAction(state, reflectionsHeader, CopyReflectionsSection);

        Widgets::SectionHeader probesHeader = MakeLightingSectionHeader(liveLighting, CopyReflectionProbesSection);
        if (Widgets::BeginSection("Reflection Probes", &probesHeader)) {
            Core::ReflectionProbeConfiguration& reflectionProbe = state->lighting.reflectionProbe;

            if (Widgets::Checkbox("Enable Reflection Probes", &reflectionProbe.bEnabled)) { changed = true; }
            if (Widgets::SliderFloat("Probe Intensity##reflectionprobe", &reflectionProbe.intensity, 0.0f, 2.0f, {.format = "%.2f", .reset = true, .resetTo = 1.0})) { changed = true; }

            ImGui::Spacing();
            if (Widgets::Button("Reset Reflection Probes")) {
                reflectionProbe = Core::ReflectionProbeConfiguration{};
                changed = true;
            }
            Widgets::EndSection();
        }
        HandleLightingSectionAction(state, probesHeader, CopyReflectionProbesSection);

        Widgets::SectionHeader diagnosticsHeader = MakeLightingSectionHeader(liveLighting, CopyDiagnosticsSection);
        if (Widgets::BeginSection("Diagnostics", &diagnosticsHeader)) {
            Widgets::SubHeader("Pipeline Overrides");
            ImGui::BeginDisabled(bIsGroundTruth);
            if (Widgets::PassFilter("Shading Override")) {
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
            if (Widgets::PassFilter("Lighting Override")) {
                Core::Arena& arena = ctx->editorArena.Get();
                Core::ArenaFixedVector<StringID> lightingPipelines = ctx->pipelineManager->GetLightingPipelinesForMode(state->lighting.lightingMode, arena);
                const int32_t pipelineCount = static_cast<int32_t>(lightingPipelines.Size());

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

            Widgets::SubHeader("Render Cache Reset");
            if (Widgets::Button("Full Renderer Clear")) {
                state->requests.pendingCacheReset = Core::RenderCacheReset::All;
            }
            Widgets::SameLine();
            if (Widgets::Button("Reset Screen History")) {
                state->requests.pendingCacheReset = Core::RenderCacheReset::ScreenHistory;
            }

            Widgets::SubHeader("Probes");
            if (Widgets::Checkbox("Brute-Force Probe Pick", &state->lighting.reflectionProbe.bBruteForcePick, "Bypass the world-grid probe bin and scan all probes per pixel.")) { changed = true; }
            if (Widgets::Checkbox("DDGI Cascade Sampling##ddgi", &state->lighting.ddgi.bCascadeSampling,
                                  "Off = consumers (composite, gather, cache shade, bounce feedback) sample local volumes only; cascade windows keep updating and still suppress sky via edge fade, so toggling back is instant.")) { changed = true; }
            if (Widgets::Checkbox("DDGI World Volume Grid Cull##ddgi", &state->lighting.ddgi.bWorldVolumeGridCull,
                                  "Off = the sampler walks every resident world volume instead of the world grid's per-cell overlap list. Same result, slower; a difference means the bin is dropping volumes.")) { changed = true; }

            if (bReSTIRMode) {
                Core::ReSTIRParams& restir = state->debug.restir;

                Widgets::SubHeader("ReSTIR");
                const char* remodulateOutputModes[] = {"Both", "Diffuse Only", "Specular Only", "Indirect Diffuse (DDGI)"};
                int currentRemodulateOutput = static_cast<int>(restir.remodulateOutput);
                if (Widgets::Combo("Remodulate Output##restir", &currentRemodulateOutput, remodulateOutputModes, IM_ARRAYSIZE(remodulateOutputModes))) {
                    restir.remodulateOutput = static_cast<Core::ReSTIRParams::RemodulateOutput>(currentRemodulateOutput);
                    changed = true;
                }
            }
            Widgets::EndSection();
        }
        HandleLightingSectionAction(state, diagnosticsHeader, CopyDiagnosticsSection);

        Widgets::EndFilter();

        if (changed && state->projectConfig.bAutoSaveLighting) {
            SaveLightingTab(state);
        }
    }
    ImGui::End();
}

bool DrawPostProcessConfig(Core::PostProcessConfiguration& pp, Engine::EngineState* profileState)
{
    constexpr Core::PostProcessConfiguration defaults{};
    bool changed = false;

    auto ppF = [&](const char* label, float* v, float def, float mn, float mx, const char* fmt = "%.3f", const char* tip = nullptr) {
        changed |= Widgets::SliderFloat(label, v, mn, mx, {.format = fmt, .tooltip = tip, .reset = true, .resetTo = def});
    };

    if (profileState != nullptr) {
        RefreshPostProcessBaseline(profileState);
    }
    const Core::PostProcessConfiguration live = pp;
    const Core::PostProcessConfiguration* baseline = postProcessBaseline.bValid ? &postProcessBaseline.config : nullptr;

    auto section = [&](const char* title, bool* enabled, PostProcessSectionCopy copy, auto&& body) {
        Widgets::SectionHeader header = profileState != nullptr ? MakeProfileSectionHeader(baseline, live, copy) : Widgets::SectionHeader{};
        header.enabled = enabled;
        if (Widgets::BeginSection(title, &header)) {
            body();
            Widgets::EndSection();
        }
        if (header.bEnabledChanged) { changed = true; }
        if (profileState != nullptr) {
            HandlePostProcessSectionAction(profileState, header, copy);
        }
    };

    section("Exposure", nullptr, CopyExposureSection, [&] {
        const char* exposureModes[] = {"Auto", "Manual EV100", "Physical Camera"};
        int exposureMode = static_cast<int>(pp.exposureMode);
        if (Widgets::Combo("Mode##exposure", &exposureMode, exposureModes, IM_ARRAYSIZE(exposureModes))) {
            pp.exposureMode = static_cast<Core::ExposureMode>(exposureMode);
            changed = true;
        }
        ppF("Target Luminance", &pp.exposureTargetLuminance, defaults.exposureTargetLuminance, 0.005f, 1.0f, "%.3f", "Post-exposure key the metered scene average maps to. 0.18 = standard mid-gray.");
        switch (pp.exposureMode) {
            case Core::ExposureMode::Auto:
                ppF("Speed Brighten", &pp.exposureSpeedBrighten, defaults.exposureSpeedBrighten, 0.1f, 10.0f, "%.1f", "Adaptation speed (1/s) while the image brightens (entering darkness). Slower than darken, like the eye.");
                ppF("Speed Darken", &pp.exposureSpeedDarken, defaults.exposureSpeedDarken, 0.1f, 10.0f, "%.1f", "Adaptation speed (1/s) while the image darkens (entering light).");
                ppF("Min EV100", &pp.exposureMinEV100, defaults.exposureMinEV100, -10.0f, 30.0f, "%.1f", "Darkest scene exposure adapts to; darker scenes stop brightening here instead of amplifying GI noise to mid-gray. EV100 = log2(average luminance * 8).");
                ppF("Max EV100", &pp.exposureMaxEV100, defaults.exposureMaxEV100, -10.0f, 30.0f, "%.1f", "Brightest scene exposure adapts to; brighter scenes stop darkening here.");
                ppF("Low Percentile", &pp.exposureLowPercentile, defaults.exposureLowPercentile, 0.0f, 0.9f, "%.2f", "Fraction of the darkest non-black pixels excluded from metering.");
                ppF("High Percentile", &pp.exposureHighPercentile, defaults.exposureHighPercentile, 0.1f, 1.0f, "%.2f", "Metering cutoff for the brightest pixels; keeps fireflies, emissives, and the sun from steering exposure.");
                break;
            case Core::ExposureMode::Manual:
                ppF("EV100##exposure", &pp.exposureManualEV100, defaults.exposureManualEV100, -10.0f, 30.0f, "%.2f", "Fixed exposure; the same image auto exposure produces when the scene meters at this EV100.");
                break;
            case Core::ExposureMode::Physical:
                ppF("Aperture (f)", &pp.cameraAperture, defaults.cameraAperture, 1.0f, 32.0f, "%.1f");
                ppF("Shutter (1/s)", &pp.cameraShutterInv, defaults.cameraShutterInv, 1.0f, 8000.0f, "%.0f", "Shutter speed denominator: 100 = 1/100 s.");
                ppF("ISO", &pp.cameraISO, defaults.cameraISO, 50.0f, 12800.0f, "%.0f");
                if (Widgets::IsShowingAll()) {
                    ImGui::Text("EV100: %.2f", std::log2(pp.cameraAperture * pp.cameraAperture * pp.cameraShutterInv * 100.0f / pp.cameraISO));
                }
                break;
        }
    });

    section("Depth of Field", &pp.bDepthOfFieldEnabled, CopyDepthOfFieldSection, [&] {
        ppF("Focus Distance", &pp.dofFocusDistance, defaults.dofFocusDistance, 0.1f, 200.0f, "%.2f", "View-space distance to the focal plane; everything at this depth stays sharp.");
        ppF("Focus Range", &pp.dofFocusRange, defaults.dofFocusRange, 0.0f, 20.0f, "%.2f", "Depth band centered on the focal plane that stays fully sharp.");
        ppF("Near Transition", &pp.dofNearTransition, defaults.dofNearTransition, 0.05f, 20.0f, "%.2f", "View units in front of the sharp band over which the blur ramps to its max near radius.");
        ppF("Far Transition", &pp.dofFarTransition, defaults.dofFarTransition, 0.05f, 200.0f, "%.2f", "View units behind the sharp band over which the blur ramps to its max far radius. The sky always sits at max.");
        ppF("Near Radius", &pp.dofNearRadiusPx, defaults.dofNearRadiusPx, 0.0f, 64.0f, "%.0f", "Max blur radius in output pixels for foreground (in front of focus).");
        ppF("Far Radius", &pp.dofFarRadiusPx, defaults.dofFarRadiusPx, 0.0f, 64.0f, "%.0f", "Max blur radius in output pixels for background (behind focus).");
    });

    section("Motion Blur", &pp.bMotionBlurEnabled, CopyMotionBlurSection, [&] {
        ppF("Velocity Scale", &pp.motionBlurVelocityScale, defaults.motionBlurVelocityScale, 0.0f, 2.0f, "%.2f", "Shutter fraction of the inter-frame displacement; 0.5 = cinematic 180-degree shutter. Shared by camera and object blur.");
        ppF("Target FPS", &pp.motionBlurTargetFps, defaults.motionBlurTargetFps, 0.0f, 240.0f, "%.0f", "Frame rate the shutter is normalized to, so blur length stays constant as fps varies and hitches do not smear. 0 = physical shutter (blur grows with frame time).");
        ppF("Depth Scale", &pp.motionBlurDepthScale, defaults.motionBlurDepthScale, 0.1f, 10.0f, "%.2f", "1 / soft depth band (view units) for foreground/background classification. 1.0 = 1m band.");
        ppF("Max Radius", &pp.motionBlurMaxRadiusPx, defaults.motionBlurMaxRadiusPx, 4.0f, 64.0f, "%.0f", "Cap on blur reach in output pixels. Faster movers saturate here and read as solid; tile dilation and sample count scale with it.");
        ppF("Object Scale", &pp.motionBlurObjectScale, defaults.motionBlurObjectScale, 0.0f, 2.0f, "%.2f", "Share of object-only motion that smears, after the camera reprojection is subtracted out. 0 = moving objects never blur.");
        ppF("Camera Rotation Scale", &pp.motionBlurCameraRotationScale, defaults.motionBlurCameraRotationScale, 0.0f, 2.0f, "%.2f", "Share of camera pan/tilt motion that smears. Drives the sky, which has no other motion.");
        ppF("Camera Translation Scale", &pp.motionBlurCameraTranslationScale, defaults.motionBlurCameraTranslationScale, 0.0f, 2.0f, "%.2f", "Share of camera dolly motion that smears. Lower than rotation keeps near geometry readable while walking.");
        ppF("Camera Dead Zone", &pp.motionBlurCameraDeadZonePx, defaults.motionBlurCameraDeadZonePx, 0.0f, 8.0f, "%.2f", "Camera blur shorter than this many output pixels is trimmed away, so idle drift and controller noise stay sharp.");
        ppF("Camera Max Radius", &pp.motionBlurCameraMaxRadiusPx, defaults.motionBlurCameraMaxRadiusPx, 1.0f, 64.0f, "%.0f", "Cap on camera blur reach in output pixels, so fast spins do not smear the whole frame. Clamped to Max Radius.");
    });

    section("Bloom", &pp.bBloomEnabled, CopyBloomSection, [&] {
        ppF("Intensity", &pp.bloomIntensity, defaults.bloomIntensity, 0.0f, 1.0f);
        ppF("Threshold", &pp.bloomThreshold, defaults.bloomThreshold, 0.0f, 2.0f, "%.2f", "Display-relative luminance where bloom starts; 1.0 = displayed white. Stable under auto-exposure.");
        ppF("Soft Threshold", &pp.bloomSoftThreshold, defaults.bloomSoftThreshold, 0.0f, 1.0f, "%.2f");
        ppF("Radius", &pp.bloomRadius, defaults.bloomRadius, 0.5f, 1.25f, "%.2f", "Tent-filter tap spacing in mip texels; above ~1.25 the upsample starts skipping texels and shimmers.");
        ppF("Clamp", &pp.bloomClamp, defaults.bloomClamp, 0.1f, 100.0f, "%.1f", "Display-relative cap applied before thresholding; secondary firefly defense behind the Karis average.");
    });

    section("Panini Projection", &pp.bPaniniEnabled, CopyPaniniSection, [&] {
        ppF("Strength##panini", &pp.paniniStrength, defaults.paniniStrength, 0.0f, 1.0f, "%.2f", "Cylindrical projection blend; 0 = rectilinear. Typical game values 0.15-0.35.");
    });

    section("Chromatic Aberration", &pp.bChromaticAberrationEnabled, CopyChromaticAberrationSection, [&] {
        ppF("Strength##chromab", &pp.chromaticAberrationStrength, defaults.chromaticAberrationStrength, 0.0f, 10.0f, "%.2f", "Pixels of R/B separation at unit radius, uniform in all directions. Above ~4 the single-tap fringes read as ghosting.");
    });

    section("Color Grading", &pp.bColorGradingEnabled, CopyColorGradingSection, [&] {
        ppF("Exposure Bias", &pp.colorGradingExposure, defaults.colorGradingExposure, -2.0f, 2.0f, "%.2f", "EV bias folded into exposure before tonemapping, so highlights roll off instead of clipping.");
        ppF("Contrast", &pp.colorGradingContrast, defaults.colorGradingContrast, 0.5f, 2.0f, "%.2f", "Log-space contrast pivoted at mid-gray.");
        ppF("Saturation", &pp.colorGradingSaturation, defaults.colorGradingSaturation, 0.0f, 2.0f, "%.2f");
        ppF("Temperature", &pp.colorGradingTemperature, defaults.colorGradingTemperature, -1.0f, 1.0f, "%.2f", "White balance warm/cool via CAT02 gains; preserves black and overall luminance.");
        ppF("Tint", &pp.colorGradingTint, defaults.colorGradingTint, -1.0f, 1.0f, "%.2f", "White balance green/magenta axis.");
    });

    section("Tonemapping", nullptr, CopyTonemappingSection, [&] {
        const char* tonemapOperators[] = {"None", "[Simple] ACES (Hill)", "[Simple] Hable", "[Simple] Reinhard", "[Simple] Lottes", "[Simple] Reinhard-Jodie", "[Simple] Clamp", "[Filmic] Hejl-Burgess-Dawson", "[Filmic] Uchimura", "[Filmic] ACES (Narkowicz)", "[Modern] AgX", "[Modern] Khronos PBR Neutral"};
        int currentItem = pp.tonemapOperator + 1;
        if (Widgets::Combo("Operator", &currentItem, tonemapOperators, IM_ARRAYSIZE(tonemapOperators))) {
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
    });

    section("Vignette", &pp.bVignetteEnabled, CopyVignetteSection, [&] {
        ppF("Strength##vignette", &pp.vignetteStrength, defaults.vignetteStrength, 0.0f, 1.0f, "%.2f");
        ppF("Radius##vignette", &pp.vignetteRadius, defaults.vignetteRadius, 0.5f, 1.0f, "%.2f");
        ppF("Smoothness##vignette", &pp.vignetteSmoothness, defaults.vignetteSmoothness, 0.05f, 1.0f, "%.2f");
        ppF("Roundness##vignette", &pp.vignetteRoundness, defaults.vignetteRoundness, 0.0f, 1.0f, "%.2f", "0 = screen-fit ellipse, 1 = circular on any aspect ratio.");
    });

    section("Sharpening", &pp.bSharpeningEnabled, CopySharpeningSection, [&] {
        ppF("Strength##sharpening", &pp.sharpeningStrength, defaults.sharpeningStrength, 0.0f, 1.0f, "%.2f", "Display-referred unsharp mask with a bounded delta; runs after tonemapping so strength is perceptually uniform.");
    });

    section("Film Grain", &pp.bFilmGrainEnabled, CopyFilmGrainSection, [&] {
        ppF("Strength##grain", &pp.grainStrength, defaults.grainStrength, 0.0f, 0.05f);
        ppF("Size##grain", &pp.grainSize, defaults.grainSize, 1.0f, 4.0f, "%.2f", "Grain cell size in pixels.");
        ppF("Response##grain", &pp.grainResponse, defaults.grainResponse, 0.0f, 1.0f, "%.2f", "0 = flat video-style noise, 1 = film response (strongest in mids, vanishing in crushed blacks and clipped whites).");
    });

    section("Dither", &pp.bDitherEnabled, CopyDitherSection, [&] {
        ppF("Strength##dither", &pp.ditherStrength, defaults.ditherStrength, 0.0f, 2.0f, "%.2f", "Amplitude in encoded 8-bit LSBs, matched to the sRGB step at each pixel. Auto-disabled while render scale rescales the output.");
    });

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
            pp.exposureMode = Core::ExposureMode::Manual;
            pp.bBloomEnabled = false;
            pp.bDepthOfFieldEnabled = false;
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

        static ImGuiTextFilter postProcessFilter;
        DrawSearchBar(postProcessFilter, "postprocessfilter");
        ImGui::Separator();

        Widgets::BeginFilter(&postProcessFilter);
        changed |= DrawPostProcessConfig(state->lighting.postProcess, state);
        Widgets::EndFilter();

        if (changed && state->projectConfig.bAutoSavePostProcess) {
            SavePostProcessTab(state);
        }
    }
    ImGui::End();
}
}
