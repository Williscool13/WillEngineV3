//
// Created by William on 2026-10-12.
//

#include "post_process_volume_component.h"

#include <cstring>

#include <imgui.h>
#include <glm/glm.hpp>

#include "engine/components/component_editor.h"

namespace Engine::Component
{
static const PostProcessOverrideField OVERRIDE_FIELDS[] = {
    {"indirectIntensity", "Indirect Intensity", "Lighting", 0.0f, 4.0f, 0.01f, false, [](Core::ViewFamily& v) { return &v.indirectIntensity; }},
    {"iblIntensity", "IBL Intensity", "Lighting", 0.0f, 4.0f, 0.01f, false, [](Core::ViewFamily& v) { return &v.iblIntensity; }},

    {"exposureTargetLuminance", "Target Luminance", "Exposure", 0.01f, 1.0f, 0.005f, false, [](Core::ViewFamily& v) { return &v.postProcessConfig.exposureTargetLuminance; }},
    {"exposureMinEV100", "Min EV100", "Exposure", -10.0f, 30.0f, 0.05f, false, [](Core::ViewFamily& v) { return &v.postProcessConfig.exposureMinEV100; }},
    {"exposureMaxEV100", "Max EV100", "Exposure", -10.0f, 30.0f, 0.05f, false, [](Core::ViewFamily& v) { return &v.postProcessConfig.exposureMaxEV100; }},
    {"exposureManualEV100", "Manual EV100", "Exposure", -10.0f, 30.0f, 0.05f, false, [](Core::ViewFamily& v) { return &v.postProcessConfig.exposureManualEV100; }},

    {"colorGradingExposure", "Exposure Bias (EV)", "Grade", -5.0f, 5.0f, 0.01f, false, [](Core::ViewFamily& v) { return &v.postProcessConfig.colorGradingExposure; }},
    {"colorGradingContrast", "Contrast", "Grade", 0.0f, 2.0f, 0.01f, false, [](Core::ViewFamily& v) { return &v.postProcessConfig.colorGradingContrast; }},
    {"colorGradingSaturation", "Saturation", "Grade", 0.0f, 2.0f, 0.01f, false, [](Core::ViewFamily& v) { return &v.postProcessConfig.colorGradingSaturation; }},
    {"colorGradingTemperature", "Temperature", "Grade", -1.0f, 1.0f, 0.01f, false, [](Core::ViewFamily& v) { return &v.postProcessConfig.colorGradingTemperature; }},
    {"colorGradingTint", "Tint", "Grade", -1.0f, 1.0f, 0.01f, false, [](Core::ViewFamily& v) { return &v.postProcessConfig.colorGradingTint; }},

    {"bloomIntensity", "Bloom Intensity", "Bloom & Vignette", 0.0f, 2.0f, 0.005f, false, [](Core::ViewFamily& v) { return &v.postProcessConfig.bloomIntensity; }},
    {"bloomThreshold", "Bloom Threshold", "Bloom & Vignette", 0.0f, 10.0f, 0.01f, false, [](Core::ViewFamily& v) { return &v.postProcessConfig.bloomThreshold; }},
    {"vignetteStrength", "Vignette Strength", "Bloom & Vignette", 0.0f, 1.0f, 0.01f, false, [](Core::ViewFamily& v) { return &v.postProcessConfig.vignetteStrength; }},

    {"fogDensity", "Fog Density", "Fog", 0.0f, 10.0f, 0.0005f, false, [](Core::ViewFamily& v) { return &v.volumetricFog.density; }},
    {"fogHeightFalloff", "Fog Height Falloff", "Fog", 0.0f, 10.0f, 0.001f, false, [](Core::ViewFamily& v) { return &v.volumetricFog.heightFalloff; }},
    {"fogAlbedo", "Fog Albedo", "Fog", 0.0f, 1.0f, 0.01f, true, [](Core::ViewFamily& v) { return &v.volumetricFog.albedo.x; }},
    {"fogAnisotropy", "Fog Anisotropy", "Fog", -0.95f, 0.95f, 0.01f, false, [](Core::ViewFamily& v) { return &v.volumetricFog.anisotropy; }},
    {"fogAmbientScale", "Fog Ambient Scale", "Fog", 0.0f, 10.0f, 0.01f, false, [](Core::ViewFamily& v) { return &v.volumetricFog.ambientScale; }},
};

Core::Span<const PostProcessOverrideField> PostProcessOverrideFields()
{
    return Core::Span<const PostProcessOverrideField>(OVERRIDE_FIELDS);
}

const PostProcessOverrideField* FindPostProcessOverrideField(std::string_view name)
{
    for (const PostProcessOverrideField& f : OVERRIDE_FIELDS) {
        if (name == f.name) {
            return &f;
        }
    }
    return nullptr;
}

void PostProcessVolumeComponent::Sanitize(PostProcessVolumeComponent& comp)
{
    comp.extents = glm::max(comp.extents, Vec3{0.0f});
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
    const Vec3 outside = glm::max(glm::abs(local) - volume.extents * glm::abs(scale), Vec3{0.0f});
    const float distance = glm::length(outside);
    if (volume.blendRadius <= 0.0f) {
        return distance > 0.0f ? 0.0f : volume.blendWeight;
    }
    return volume.blendWeight * (1.0f - glm::clamp(distance / volume.blendRadius, 0.0f, 1.0f));
}

static bool HasOverride(const PostProcessVolumeComponent& comp, const char* name)
{
    for (const PostProcessOverride& o : comp.overrides) {
        if (o.field.View() == name) {
            return true;
        }
    }
    return false;
}

static void DrawOverrides(Core::ViewFamily& viewFamily, Engine::EditContext& edit)
{
    const PostProcessVolumeComponent before = edit.Get<PostProcessVolumeComponent>();
    PostProcessVolumeComponent comp = before;
    bool bCommit = false;
    int32_t removeIndex = -1;

    ImGui::SeparatorText("Overrides");
    for (uint32_t i = 0; i < comp.overrides.Size(); ++i) {
        PostProcessOverride& o = comp.overrides[i];
        const PostProcessOverrideField* field = FindPostProcessOverrideField(o.field.View());
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton("X")) {
            removeIndex = static_cast<int32_t>(i);
        }
        ImGui::SameLine();
        if (!field) {
            ImGui::TextDisabled("Unknown field '%s'", o.field.c_str());
        }
        else if (field->bColor) {
            ImGui::ColorEdit3(field->label, &o.value.x, ImGuiColorEditFlags_Float);
            bCommit |= ImGui::IsItemDeactivatedAfterEdit();
        }
        else {
            ImGui::DragFloat(field->label, &o.value.x, field->speed, field->min, field->max, "%.4f");
            bCommit |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        comp.overrides.RemoveAt(static_cast<size_t>(removeIndex));
        bCommit = true;
    }

    ImGui::BeginDisabled(comp.overrides.IsFull());
    if (ImGui::Button("Add Override##ppv")) {
        ImGui::OpenPopup("ppv_add_override");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("ppv_add_override")) {
        const char* group = nullptr;
        for (const PostProcessOverrideField& f : OVERRIDE_FIELDS) {
            if (HasOverride(comp, f.name)) {
                continue;
            }
            if (group == nullptr || std::strcmp(group, f.group) != 0) {
                group = f.group;
                ImGui::SeparatorText(group);
            }
            if (ImGui::MenuItem(f.label)) {
                const float* current = f.resolve(viewFamily);
                PostProcessOverride o;
                o.field = Core::InlineString<32>(f.name);
                o.value = f.bColor ? Vec3{current[0], current[1], current[2]} : Vec3{current[0], 0.0f, 0.0f};
                comp.overrides.PushBack(o);
                bCommit = true;
            }
        }
        ImGui::EndPopup();
    }

    edit.PreviewDiff(before, comp);
    if (bCommit) {
        edit.Commit<PostProcessVolumeComponent>();
    }
}

Engine::ComponentEditorResult PostProcessVolumeComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    bool open = ImGui::CollapsingHeader("Post Process Volume", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteppv");
    ImGui::PopStyleColor();

    if (open) {
        EditWidgets::Checkbox(edit, "Enabled##ppv", &PostProcessVolumeComponent::bEnabled);
        EditWidgets::Checkbox(edit, "Unbound##ppv", &PostProcessVolumeComponent::bUnbound);
        EditWidgets::DragInt(edit, "Priority##ppv", &PostProcessVolumeComponent::priority, 1.0f, -100, 100);
        ImGui::BeginDisabled(edit.Get<PostProcessVolumeComponent>().bUnbound);
        EditWidgets::DragFloat3(edit, "Extents##ppv", &PostProcessVolumeComponent::extents, 0.05f, 0.0f, 1000.0f);
        EditWidgets::DragFloat(edit, "Blend Radius##ppv", &PostProcessVolumeComponent::blendRadius, 0.05f, 0.0f, 100.0f);
        ImGui::EndDisabled();
        EditWidgets::SliderFloat(edit, "Blend Weight##ppv", &PostProcessVolumeComponent::blendWeight, 0.0f, 1.0f);

        if (edit.IsMulti()) {
            ImGui::TextDisabled("Overrides: single selection only.");
        }
        else {
            DrawOverrides(viewFamily, edit);
        }
    }

    return {.bRequestRemoval = remove};
}
}
