//
// Created by William on 2026-10-12.
//

#include "post_process_volume_component.h"

#include <cmath>
#include <cstring>

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/glm.hpp>

#include "engine/components/component_editor.h"
#include "engine/components/core_components.h"
#include "engine/editor/editor_gizmo_helpers.h"
#include "engine/include/engine_context.h"
#include "engine/input/engine_actions.h"
#include "render/interface/render_interface.h"

namespace Engine::Component
{
static PostProcessOverrideField FloatField(const char* name, const char* label, const char* group, float min, float max, float speed, void* (*resolve)(Core::ViewFamily&))
{
    return {name, label, group, PostProcessOverrideKind::Float, min, max, speed, resolve, {}, 0};
}

static PostProcessOverrideField ColorField(const char* name, const char* label, const char* group, void* (*resolve)(Core::ViewFamily&))
{
    return {name, label, group, PostProcessOverrideKind::Color, 0.0f, 1.0f, 0.01f, resolve, {}, 0};
}

static PostProcessOverrideField BoolField(const char* name, const char* label, const char* group, void* (*resolve)(Core::ViewFamily&))
{
    return {name, label, group, PostProcessOverrideKind::Bool, 0.0f, 1.0f, 0.0f, resolve, {}, 0};
}

static PostProcessOverrideField EnumField(const char* name, const char* label, const char* group, Core::Span<const char* const> options, int32_t optionOffset, void* (*resolve)(Core::ViewFamily&))
{
    return {name, label, group, PostProcessOverrideKind::Enum, 0.0f, 0.0f, 0.0f, resolve, options, optionOffset};
}

static const char* const EXPOSURE_MODES[] = {"Auto", "Manual EV100", "Physical Camera"};
static const char* const TONEMAP_OPERATORS[] = {"None", "[Simple] ACES (Hill)", "[Simple] Hable", "[Simple] Reinhard", "[Simple] Lottes", "[Simple] Reinhard-Jodie", "[Simple] Clamp", "[Filmic] Hejl-Burgess-Dawson", "[Filmic] Uchimura", "[Filmic] ACES (Narkowicz)", "[Modern] AgX", "[Modern] Khronos PBR Neutral"};

#define PP_RESOLVE(member) [](Core::ViewFamily& v) -> void* { return &v.postProcessConfig.member; }
#define FOG_RESOLVE(member) [](Core::ViewFamily& v) -> void* { return &v.volumetricFog.member; }

static const PostProcessOverrideField OVERRIDE_FIELDS[] = {
    EnumField("exposureMode", "Exposure Mode", "Exposure", EXPOSURE_MODES, 0, PP_RESOLVE(exposureMode)),
    FloatField("exposureTargetLuminance", "Target Luminance", "Exposure", 0.005f, 1.0f, 0.005f, PP_RESOLVE(exposureTargetLuminance)),
    FloatField("exposureSpeedBrighten", "Speed Brighten", "Exposure", 0.1f, 10.0f, 0.05f, PP_RESOLVE(exposureSpeedBrighten)),
    FloatField("exposureSpeedDarken", "Speed Darken", "Exposure", 0.1f, 10.0f, 0.05f, PP_RESOLVE(exposureSpeedDarken)),
    FloatField("exposureMinEV100", "Min EV100", "Exposure", -10.0f, 30.0f, 0.05f, PP_RESOLVE(exposureMinEV100)),
    FloatField("exposureMaxEV100", "Max EV100", "Exposure", -10.0f, 30.0f, 0.05f, PP_RESOLVE(exposureMaxEV100)),
    FloatField("exposureManualEV100", "Manual EV100", "Exposure", -10.0f, 30.0f, 0.05f, PP_RESOLVE(exposureManualEV100)),
    FloatField("cameraAperture", "Aperture (f)", "Exposure", 1.0f, 32.0f, 0.1f, PP_RESOLVE(cameraAperture)),
    FloatField("cameraShutterInv", "Shutter (1/s)", "Exposure", 1.0f, 8000.0f, 1.0f, PP_RESOLVE(cameraShutterInv)),
    FloatField("cameraISO", "ISO", "Exposure", 50.0f, 12800.0f, 10.0f, PP_RESOLVE(cameraISO)),

    BoolField("bDepthOfFieldEnabled", "Depth of Field", "Depth of Field", PP_RESOLVE(bDepthOfFieldEnabled)),
    FloatField("dofFocusDistance", "Focus Distance", "Depth of Field", 0.1f, 200.0f, 0.05f, PP_RESOLVE(dofFocusDistance)),
    FloatField("dofFocusRange", "Focus Range", "Depth of Field", 0.0f, 20.0f, 0.05f, PP_RESOLVE(dofFocusRange)),
    FloatField("dofNearTransition", "Near Transition", "Depth of Field", 0.05f, 20.0f, 0.05f, PP_RESOLVE(dofNearTransition)),
    FloatField("dofFarTransition", "Far Transition", "Depth of Field", 0.05f, 200.0f, 0.1f, PP_RESOLVE(dofFarTransition)),
    FloatField("dofNearRadiusPx", "Near Radius", "Depth of Field", 0.0f, 64.0f, 0.25f, PP_RESOLVE(dofNearRadiusPx)),
    FloatField("dofFarRadiusPx", "Far Radius", "Depth of Field", 0.0f, 64.0f, 0.25f, PP_RESOLVE(dofFarRadiusPx)),

    BoolField("bMotionBlurEnabled", "Motion Blur", "Motion Blur", PP_RESOLVE(bMotionBlurEnabled)),
    FloatField("motionBlurVelocityScale", "Velocity Scale", "Motion Blur", 0.0f, 2.0f, 0.01f, PP_RESOLVE(motionBlurVelocityScale)),
    FloatField("motionBlurMaxRadiusPx", "Max Radius", "Motion Blur", 4.0f, 64.0f, 0.25f, PP_RESOLVE(motionBlurMaxRadiusPx)),

    BoolField("bBloomEnabled", "Bloom", "Bloom", PP_RESOLVE(bBloomEnabled)),
    FloatField("bloomIntensity", "Bloom Intensity", "Bloom", 0.0f, 1.0f, 0.005f, PP_RESOLVE(bloomIntensity)),
    FloatField("bloomThreshold", "Bloom Threshold", "Bloom", 0.0f, 2.0f, 0.01f, PP_RESOLVE(bloomThreshold)),
    FloatField("bloomSoftThreshold", "Bloom Soft Threshold", "Bloom", 0.0f, 1.0f, 0.01f, PP_RESOLVE(bloomSoftThreshold)),
    FloatField("bloomRadius", "Bloom Radius", "Bloom", 0.5f, 1.25f, 0.005f, PP_RESOLVE(bloomRadius)),

    BoolField("bPaniniEnabled", "Panini Projection", "Panini Projection", PP_RESOLVE(bPaniniEnabled)),
    FloatField("paniniStrength", "Panini Strength", "Panini Projection", 0.0f, 1.0f, 0.01f, PP_RESOLVE(paniniStrength)),

    BoolField("bChromaticAberrationEnabled", "Chromatic Aberration", "Chromatic Aberration", PP_RESOLVE(bChromaticAberrationEnabled)),
    FloatField("chromaticAberrationStrength", "Chromatic Aberration Strength", "Chromatic Aberration", 0.0f, 10.0f, 0.01f, PP_RESOLVE(chromaticAberrationStrength)),

    BoolField("bColorGradingEnabled", "Color Grading", "Color Grading", PP_RESOLVE(bColorGradingEnabled)),
    FloatField("colorGradingExposure", "Exposure Bias (EV)", "Color Grading", -5.0f, 5.0f, 0.01f, PP_RESOLVE(colorGradingExposure)),
    FloatField("colorGradingContrast", "Contrast", "Color Grading", 0.0f, 2.0f, 0.01f, PP_RESOLVE(colorGradingContrast)),
    FloatField("colorGradingSaturation", "Saturation", "Color Grading", 0.0f, 2.0f, 0.01f, PP_RESOLVE(colorGradingSaturation)),
    FloatField("colorGradingTemperature", "Temperature", "Color Grading", -1.0f, 1.0f, 0.01f, PP_RESOLVE(colorGradingTemperature)),
    FloatField("colorGradingTint", "Tint", "Color Grading", -1.0f, 1.0f, 0.01f, PP_RESOLVE(colorGradingTint)),

    EnumField("tonemapOperator", "Tonemap Operator", "Tonemapping", TONEMAP_OPERATORS, -1, PP_RESOLVE(tonemapOperator)),

    BoolField("bVignetteEnabled", "Vignette", "Vignette", PP_RESOLVE(bVignetteEnabled)),
    FloatField("vignetteStrength", "Vignette Strength", "Vignette", 0.0f, 1.0f, 0.01f, PP_RESOLVE(vignetteStrength)),
    FloatField("vignetteRadius", "Vignette Radius", "Vignette", 0.5f, 1.0f, 0.01f, PP_RESOLVE(vignetteRadius)),
    FloatField("vignetteSmoothness", "Vignette Smoothness", "Vignette", 0.05f, 1.0f, 0.01f, PP_RESOLVE(vignetteSmoothness)),
    FloatField("vignetteRoundness", "Vignette Roundness", "Vignette", 0.0f, 1.0f, 0.01f, PP_RESOLVE(vignetteRoundness)),

    BoolField("bSharpeningEnabled", "Sharpening", "Sharpening", PP_RESOLVE(bSharpeningEnabled)),
    FloatField("sharpeningStrength", "Sharpening Strength", "Sharpening", 0.0f, 1.0f, 0.01f, PP_RESOLVE(sharpeningStrength)),

    BoolField("bFilmGrainEnabled", "Film Grain", "Film Grain", PP_RESOLVE(bFilmGrainEnabled)),
    FloatField("grainStrength", "Grain Strength", "Film Grain", 0.0f, 0.05f, 0.0005f, PP_RESOLVE(grainStrength)),
    FloatField("grainSize", "Grain Size", "Film Grain", 1.0f, 4.0f, 0.01f, PP_RESOLVE(grainSize)),
    FloatField("grainResponse", "Grain Response", "Film Grain", 0.0f, 1.0f, 0.01f, PP_RESOLVE(grainResponse)),

    FloatField("fogDensity", "Fog Density", "Fog", 0.0f, 10.0f, 0.0005f, FOG_RESOLVE(density)),
    FloatField("fogHeightFalloff", "Fog Height Falloff", "Fog", 0.0f, 10.0f, 0.001f, FOG_RESOLVE(heightFalloff)),
    ColorField("fogAlbedo", "Fog Albedo", "Fog", FOG_RESOLVE(albedo)),
    FloatField("fogAnisotropy", "Fog Anisotropy", "Fog", -0.95f, 0.95f, 0.01f, FOG_RESOLVE(anisotropy)),
    FloatField("fogAmbientScale", "Fog Ambient Scale", "Fog", 0.0f, 10.0f, 0.01f, FOG_RESOLVE(ambientScale)),
};

#undef PP_RESOLVE
#undef FOG_RESOLVE

const PostProcessOverrideField* FindPostProcessOverrideField(std::string_view name)
{
    for (const PostProcessOverrideField& f : OVERRIDE_FIELDS) {
        if (name == f.name) {
            return &f;
        }
    }
    return nullptr;
}

Vec3 ReadPostProcessOverrideField(const PostProcessOverrideField& field, Core::ViewFamily& viewFamily)
{
    void* p = field.resolve(viewFamily);
    switch (field.kind) {
        case PostProcessOverrideKind::Float:
            return {*static_cast<float*>(p), 0.0f, 0.0f};
        case PostProcessOverrideKind::Color:
            return *static_cast<Vec3*>(p);
        case PostProcessOverrideKind::Bool:
            return {*static_cast<bool*>(p) ? 1.0f : 0.0f, 0.0f, 0.0f};
        case PostProcessOverrideKind::Enum: {
            int32_t i;
            std::memcpy(&i, p, sizeof(i));
            return {static_cast<float>(i), 0.0f, 0.0f};
        }
    }
    return Vec3{0.0f};
}

void ApplyPostProcessOverride(const PostProcessOverrideField& field, Core::ViewFamily& viewFamily, const Vec3& value, float weight)
{
    void* p = field.resolve(viewFamily);
    switch (field.kind) {
        case PostProcessOverrideKind::Float: {
            float& f = *static_cast<float*>(p);
            f = glm::mix(f, value.x, weight);
            break;
        }
        case PostProcessOverrideKind::Color: {
            Vec3& c = *static_cast<Vec3*>(p);
            c = glm::mix(c, value, weight);
            break;
        }
        case PostProcessOverrideKind::Bool:
            if (weight >= 0.5f) {
                *static_cast<bool*>(p) = value.x >= 0.5f;
            }
            break;
        case PostProcessOverrideKind::Enum:
            if (weight >= 0.5f) {
                const int32_t i = static_cast<int32_t>(std::lround(value.x));
                std::memcpy(p, &i, sizeof(i));
            }
            break;
    }
}

void PostProcessVolumeComponent::Sanitize(PostProcessVolumeComponent& comp)
{
    comp.blendRadius = glm::max(comp.blendRadius, 0.0f);
    comp.blendWeight = glm::clamp(comp.blendWeight, 0.0f, 1.0f);
}

float PostProcessVolumeComponent::CameraWeight(const PostProcessVolumeComponent& volume, const Vec3& center, const Quat& rotation, const Vec3& scale, const Vec3& cameraPos)
{
    if (!volume.bEnabled) {
        return 0.0f;
    }
    if (volume.bUnbound) {
        return volume.blendWeight;
    }
    const Vec3 local = glm::inverse(rotation) * (cameraPos - center);
    const Vec3 outside = glm::max(glm::abs(local) - glm::abs(scale), Vec3{0.0f});
    const float distance = glm::length(outside);
    if (volume.blendRadius <= 0.0f) {
        return distance > 0.0f ? 0.0f : volume.blendWeight;
    }
    return volume.blendWeight * (1.0f - glm::clamp(distance / volume.blendRadius, 0.0f, 1.0f));
}

void PostProcessVolumeComponent::DrawBounds(Core::ViewFamily& viewFamily, const PostProcessVolumeComponent& volume, const Vec3& center, const Quat& rotation, const Vec3& scale, float lineWidth, bool bSelected)
{
    if (volume.bUnbound) {
        return;
    }
    constexpr Vec4 boxColor{0.7f, 0.4f, 1.0f, 1.0f};
    constexpr Vec4 bandColor{0.4f, 0.25f, 0.6f, 1.0f};
    constexpr Vec4 disabledColor{0.45f, 0.45f, 0.45f, 1.0f};
    const Vec3 half = glm::abs(scale);
    DEBUG_ADD_BOX(viewFamily.debugBoxes, {center, half, rotation, volume.bEnabled ? boxColor : disabledColor, lineWidth});
    if (bSelected && volume.blendRadius > 0.0f) {
        DEBUG_ADD_BOX(viewFamily.debugBoxes, {center, half + Vec3{volume.blendRadius}, rotation, volume.bEnabled ? bandColor : disabledColor, lineWidth});
    }
}

static int32_t FindOverride(const PostProcessVolumeComponent& comp, std::string_view name)
{
    for (uint32_t i = 0; i < comp.overrides.Size(); ++i) {
        if (comp.overrides[i].field.View() == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

static void AddToMissing(Engine::EditContext& edit, const char* name, const Vec3& value)
{
    edit.Modify<PostProcessVolumeComponent>([name, &value](PostProcessVolumeComponent& c) {
        if (FindOverride(c, name) >= 0 || c.overrides.IsFull()) { return; }
        PostProcessOverride o;
        o.field = Core::InlineString<32>(name);
        o.value = value;
        c.overrides.PushBack(o);
    });
}

static void RemoveFromAll(Engine::EditContext& edit, std::string_view name)
{
    edit.Modify<PostProcessVolumeComponent>([name](PostProcessVolumeComponent& c) {
        const int32_t index = FindOverride(c, name);
        if (index >= 0) {
            c.overrides.RemoveAt(static_cast<size_t>(index));
        }
    });
}

static void PreviewValue(Engine::EditContext& edit, const char* name, const Vec3& value)
{
    edit.Preview<PostProcessVolumeComponent>([name, &value](PostProcessVolumeComponent& c) {
        const int32_t index = FindOverride(c, name);
        if (index >= 0) {
            c.overrides[static_cast<size_t>(index)].value = value;
        }
    });
}

static void DrawOverrideRow(Engine::EditContext& edit, const PostProcessOverrideField& f, Vec3 value, bool bMixed)
{
    switch (f.kind) {
        case PostProcessOverrideKind::Float:
            if (ImGui::DragFloat(f.label, &value.x, f.speed, f.min, f.max, bMixed ? "--" : "%.4f")) {
                PreviewValue(edit, f.name, value);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                edit.Commit<PostProcessVolumeComponent>();
            }
            break;
        case PostProcessOverrideKind::Color:
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, bMixed);
            if (ImGui::ColorEdit3(f.label, &value.x, ImGuiColorEditFlags_Float)) {
                PreviewValue(edit, f.name, value);
            }
            ImGui::PopItemFlag();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                edit.Commit<PostProcessVolumeComponent>();
            }
            break;
        case PostProcessOverrideKind::Bool: {
            bool b = value.x >= 0.5f;
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, bMixed);
            const bool bChanged = ImGui::Checkbox(f.label, &b);
            ImGui::PopItemFlag();
            if (bChanged) {
                PreviewValue(edit, f.name, Vec3{b ? 1.0f : 0.0f, 0.0f, 0.0f});
                edit.Commit<PostProcessVolumeComponent>();
            }
            break;
        }
        case PostProcessOverrideKind::Enum: {
            const int32_t current = static_cast<int32_t>(std::lround(value.x)) - f.optionOffset;
            const int32_t count = static_cast<int32_t>(f.options.Size());
            const char* preview = bMixed ? "--" : (current >= 0 && current < count ? f.options[current] : "?");
            if (ImGui::BeginCombo(f.label, preview)) {
                for (int32_t i = 0; i < count; ++i) {
                    if (ImGui::Selectable(f.options[i], !bMixed && i == current)) {
                        PreviewValue(edit, f.name, Vec3{static_cast<float>(i + f.optionOffset), 0.0f, 0.0f});
                        edit.Commit<PostProcessVolumeComponent>();
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
    }
}

static void DrawOverrides(Core::ViewFamily& viewFamily, Engine::EditContext& edit)
{
    const entt::registry& registry = edit.Registry();
    uint32_t volumeCount = 0;
    bool bAllFull = true;
    for (const entt::entity e : edit.Targets()) {
        if (const auto* c = registry.try_get<PostProcessVolumeComponent>(e)) {
            ++volumeCount;
            bAllFull = bAllFull && c->overrides.IsFull();
        }
    }

    ImGui::SeparatorText("Overrides");
    for (const PostProcessOverrideField& f : OVERRIDE_FIELDS) {
        uint32_t holders = 0;
        bool bMixed = false;
        Vec3 value{0.0f};
        for (const entt::entity e : edit.Targets()) {
            const auto* c = registry.try_get<PostProcessVolumeComponent>(e);
            if (c == nullptr) { continue; }
            const int32_t index = FindOverride(*c, f.name);
            if (index < 0) { continue; }
            const Vec3& v = c->overrides[static_cast<size_t>(index)].value;
            if (holders == 0) {
                value = v;
            }
            else if (v != value) {
                bMixed = true;
            }
            ++holders;
        }
        if (holders == 0) { continue; }

        ImGui::PushID(f.name);
        if (ImGui::SmallButton("X")) {
            RemoveFromAll(edit, f.name);
            ImGui::PopID();
            continue;
        }
        ImGui::SameLine();
        if (holders < volumeCount) {
            if (ImGui::SmallButton("+")) {
                AddToMissing(edit, f.name, value);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%u/%u", holders, volumeCount);
            ImGui::SameLine();
        }
        DrawOverrideRow(edit, f, value, bMixed);
        ImGui::PopID();
    }

    const PostProcessVolumeComponent& primary = edit.Get<PostProcessVolumeComponent>();
    for (uint32_t i = 0; i < primary.overrides.Size(); ++i) {
        const PostProcessOverride& o = primary.overrides[i];
        if (FindPostProcessOverrideField(o.field.View()) != nullptr) { continue; }
        ImGui::PushID(static_cast<int>(i));
        const Core::InlineString<32> unknown = o.field;
        if (ImGui::SmallButton("X")) {
            RemoveFromAll(edit, unknown.View());
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Unknown field '%s'", unknown.c_str());
        ImGui::PopID();
    }

    ImGui::BeginDisabled(bAllFull);
    if (ImGui::Button("Add Override##ppv")) {
        ImGui::OpenPopup("ppv_add_override");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("ppv_add_override")) {
        const char* group = nullptr;
        for (const PostProcessOverrideField& f : OVERRIDE_FIELDS) {
            uint32_t holders = 0;
            for (const entt::entity e : edit.Targets()) {
                const auto* c = registry.try_get<PostProcessVolumeComponent>(e);
                if (c != nullptr && FindOverride(*c, f.name) >= 0) {
                    ++holders;
                }
            }
            if (holders == volumeCount) {
                continue;
            }
            if (group == nullptr || std::strcmp(group, f.group) != 0) {
                group = f.group;
                ImGui::SeparatorText(group);
            }
            if (ImGui::MenuItem(f.label)) {
                AddToMissing(edit, f.name, ReadPostProcessOverrideField(f, viewFamily));
            }
        }
        ImGui::EndPopup();
    }
}

static void DrawFaceHandles(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const TransformComponent& transform)
{
    auto* ctx = edit.Registry().ctx().get<Engine::EngineContext*>();
    Engine::EngineState* state = edit.State();
    const auto& vd = viewFamily.mainView.currentViewData;
    const Vec4 viewport{
        static_cast<float>(ctx->windowContext.viewportOffsetX),
        static_cast<float>(ctx->windowContext.viewportOffsetY),
        static_cast<float>(ctx->windowContext.viewportWidth),
        static_cast<float>(ctx->windowContext.viewportHeight),
    };

    const Vec3 center = transform.translation;
    const Quat rot = transform.rotation;
    const Vec3 axes[3] = {rot * Vec3(1.0f, 0.0f, 0.0f), rot * Vec3(0.0f, 1.0f, 0.0f), rot * Vec3(0.0f, 0.0f, 1.0f)};
    const ImU32 colors[3] = {Editor::COLOR_AXIS_X, Editor::COLOR_AXIS_Y, Editor::COLOR_AXIS_Z};
    for (int i = 0; i < 3; ++i) {
        const float half = glm::abs(transform.scale[i]);
        for (int s = 0; s < 2; ++s) {
            const Vec3 outward = s == 0 ? axes[i] : -axes[i];
            Editor::AxisDotHandle(Editor::DotHandleId::POST_PROCESS_VOLUME_BOUNDS_BASE + i * 2 + s, center + outward * half, outward,
                                  vd.view, vd.proj, viewport, vd.cameraPos, state,
                                  [&](Vec3 newPt) {
                                      const Vec3 opposite = center - outward * half;
                                      const float newHalf = glm::max(0.05f, glm::dot(newPt - opposite, outward) * 0.5f);
                                      edit.Preview<TransformComponent>([&](TransformComponent& t) {
                                          t.scale[i] = newHalf;
                                          t.translation = opposite + outward * newHalf;
                                      });
                                  },
                                  colors[i]);
        }
    }
}

Engine::ComponentEditorResult PostProcessVolumeComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    static entt::entity editEntity = entt::null;
    static bool bEditing = false;

    if (editEntity != entity || edit.IsMulti()) {
        editEntity = entity;
        bEditing = false;
    }

    Engine::EngineState* state = edit.State();
    if (bEditing) {
        state->editor.bExclusiveGizmoActive = true;
        const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (!popupOpen && state->input.GetActionState(Actions::ACTION_ESCAPE).down) {
            bEditing = false;
        }
    }

    bool open = ImGui::CollapsingHeader("Post Process Volume", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteppv");
    ImGui::PopStyleColor();

    if (open) {
        EditWidgets::Checkbox(edit, "Enabled##ppv", &PostProcessVolumeComponent::bEnabled);
        EditWidgets::Checkbox(edit, "Unbound##ppv", &PostProcessVolumeComponent::bUnbound);
        EditWidgets::DragInt(edit, "Priority##ppv", &PostProcessVolumeComponent::priority, 1.0f, -100, 100);
        const bool bUnbound = edit.Get<PostProcessVolumeComponent>().bUnbound;
        if (bUnbound) {
            bEditing = false;
        }
        ImGui::BeginDisabled(bUnbound);
        EditWidgets::DragFloat(edit, "Blend Radius##ppv", &PostProcessVolumeComponent::blendRadius, 0.05f, 0.0f, 100.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, bEditing ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
        ImGui::BeginDisabled(edit.IsMulti() || ((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditing));
        if (ImGui::Button(bEditing ? "Done##ppv" : "Edit Bounds##ppv")) {
            bEditing = !bEditing;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
        EditWidgets::SliderFloat(edit, "Blend Weight##ppv", &PostProcessVolumeComponent::blendWeight, 0.0f, 1.0f);
        DrawOverrides(viewFamily, edit);
    }

    const auto* transform = registry.try_get<TransformComponent>(entity);
    if (transform && bEditing) {
        DrawFaceHandles(viewFamily, edit, *transform);
        DrawBounds(viewFamily, edit.Get<PostProcessVolumeComponent>(),transform->translation, transform->rotation, transform->scale, state->projectConfig.reflectionProbeLineWidth, true);
    }

    return {.bRequestRemoval = remove};
}
}
