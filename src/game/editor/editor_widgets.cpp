//
// Created by William on 2026-06-13.
//

#include "editor_widgets.h"

#include <cstring>

#include "imgui.h"

namespace Game::Widgets
{
constexpr float kInputWidth = 70.0f;
constexpr float kMinSliderWidth = 60.0f;

static float SliderWidth(bool hasReset, float spacing, float resetWidth)
{
    const float reserved = kInputWidth + spacing + (hasReset ? resetWidth + spacing : 0.0f);
    const float width = ImGui::CalcItemWidth() - reserved;
    return width < kMinSliderWidth ? kMinSliderWidth : width;
}

static void DrawTooltip(const char* tip)
{
    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tip);
    }
}

static void DrawName(const char* name)
{
    if (const char* hashes = std::strstr(name, "##")) {
        ImGui::TextUnformatted(name, hashes);
    }
    else {
        ImGui::TextUnformatted(name);
    }
}

bool SliderFloat(const char* name, float* v, float vMin, float vMax, const SliderOpts& opts)
{
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float resetWidth = ImGui::CalcTextSize("R").x + ImGui::GetStyle().FramePadding.x * 2.0f;

    bool changed = false;
    ImGui::PushID(name);

    ImGui::SetNextItemWidth(SliderWidth(opts.reset, spacing, resetWidth));
    if (ImGui::SliderFloat("##s", v, vMin, vMax, "")) { changed = true; }
    DrawTooltip(opts.tooltip);

    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(kInputWidth);
    if (ImGui::InputFloat("##i", v, 0.0f, 0.0f, opts.format)) { changed = true; }

    if (opts.reset) {
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("R")) {
            *v = static_cast<float>(opts.resetTo);
            changed = true;
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Reset to %g", opts.resetTo); }
    }

    ImGui::SameLine(0.0f, spacing);
    DrawName(name);
    DrawTooltip(opts.tooltip);

    ImGui::PopID();
    return changed;
}

bool SliderInt(const char* name, int* v, int vMin, int vMax, const SliderOpts& opts)
{
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float resetWidth = ImGui::CalcTextSize("R").x + ImGui::GetStyle().FramePadding.x * 2.0f;

    bool changed = false;
    ImGui::PushID(name);

    ImGui::SetNextItemWidth(SliderWidth(opts.reset, spacing, resetWidth));
    if (ImGui::SliderInt("##s", v, vMin, vMax, "")) { changed = true; }
    DrawTooltip(opts.tooltip);

    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(kInputWidth);
    if (ImGui::InputInt("##i", v, 0, 0)) { changed = true; }

    if (opts.reset) {
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("R")) {
            *v = static_cast<int>(opts.resetTo);
            changed = true;
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Reset to %d", static_cast<int>(opts.resetTo)); }
    }

    ImGui::SameLine(0.0f, spacing);
    DrawName(name);
    DrawTooltip(opts.tooltip);

    ImGui::PopID();
    return changed;
}
}
