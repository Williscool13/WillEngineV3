//
// Created by William on 2026-06-13.
//

#include "editor_widgets.h"

#include <cmath>
#include <cstring>

#include "imgui.h"

#include "core/containers/inline_string.h"
#include "render/render-graph/render_graph_resources.h"

namespace Engine::Widgets
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

static float ClampF(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

struct FilterSection
{
    const char* title;
    SectionHeader* header;
    bool bEmitted;
    bool bShowAll;
    bool bSubHeaderShowAll;
};

constexpr int32_t MAX_FILTER_SECTION_DEPTH = 8;
static const ImGuiTextFilter* activeFilter = nullptr;
static FilterSection filterSections[MAX_FILTER_SECTION_DEPTH];
static int32_t filterSectionDepth = 0;

static bool IsFiltering()
{
    return activeFilter != nullptr && activeFilter->IsActive();
}

static bool LabelPasses(const char* label)
{
    return activeFilter->PassFilter(label, std::strstr(label, "##"));
}

static void DrawSectionEnabled(const char* title, SectionHeader* header)
{
    if (header == nullptr || header->enabled == nullptr) { return; }
    ImGui::PushID(title);
    if (ImGui::Checkbox("##enabled", header->enabled)) { header->bEnabledChanged = true; }
    ImGui::PopID();
    ImGui::SameLine();
}

static void SectionTitleText(const char* title, SectionHeader* header)
{
    DrawSectionEnabled(title, header);
    if (header != nullptr && header->bDirty) {
        ImGui::SeparatorText(Core::InlineString<128>::Format("%s *", title).c_str());
    }
    else {
        ImGui::SeparatorText(title);
    }
}

static void EmitPendingTitles()
{
    for (int32_t i = 0; i < filterSectionDepth; ++i) {
        if (!filterSections[i].bEmitted) {
            SectionTitleText(filterSections[i].title, filterSections[i].header);
            filterSections[i].bEmitted = true;
        }
    }
}

static void DrawSectionButtons(SectionHeader& header, float rightEdge)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float saveWidth = ImGui::CalcTextSize("Save").x + style.FramePadding.x * 2.0f;
    const float revertWidth = ImGui::CalcTextSize("Revert").x + style.FramePadding.x * 2.0f;
    ImGui::SameLine(rightEdge - saveWidth - revertWidth - style.ItemSpacing.x);

    ImGui::BeginDisabled(!header.bCanSave);
    if (ImGui::Button("Save")) { header.action = SectionAction::Save; }
    ImGui::EndDisabled();
    if (!header.bCanSave) { DrawTooltip(header.disabledTooltip); }

    ImGui::SameLine();
    ImGui::BeginDisabled(!header.bCanRevert);
    if (ImGui::Button("Revert")) { header.action = SectionAction::Revert; }
    ImGui::EndDisabled();
    if (!header.bCanRevert) { DrawTooltip(header.disabledTooltip); }
}

void BeginFilter(const ImGuiTextFilter* filter)
{
    activeFilter = filter;
    filterSectionDepth = 0;
}

void EndFilter()
{
    activeFilter = nullptr;
    filterSectionDepth = 0;
}

bool IsShowingAll()
{
    if (!IsFiltering()) { return true; }
    if (filterSectionDepth == 0) { return false; }
    const FilterSection& section = filterSections[filterSectionDepth - 1];
    return section.bShowAll || section.bSubHeaderShowAll;
}

bool PassFilter(const char* label)
{
    if (IsShowingAll()) { return true; }
    if (!LabelPasses(label)) { return false; }
    EmitPendingTitles();
    return true;
}

bool BeginSection(const char* title, SectionHeader* header)
{
    if (filterSectionDepth >= MAX_FILTER_SECTION_DEPTH) { return false; }

    const bool bDirty = header != nullptr && header->bDirty;
    bool bShowAll = true;
    bool bEmitted = true;
    if (!IsFiltering()) {
        const bool bButtons = header != nullptr && header->bSaveRevert;
        const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        DrawSectionEnabled(title, header);
        const auto label = Core::InlineString<128>::Format(bDirty ? "%s *###%s" : "%s###%s", title, title);
        const bool bOpen = ImGui::CollapsingHeader(label.c_str(), bButtons ? ImGuiTreeNodeFlags_AllowOverlap : ImGuiTreeNodeFlags_None);
        if (bButtons) {
            ImGui::PushID(title);
            DrawSectionButtons(*header, rightEdge);
            ImGui::PopID();
        }
        if (!bOpen) { return false; }
    }
    else if (IsShowingAll() || LabelPasses(title)) {
        EmitPendingTitles();
        SectionTitleText(title, header);
    }
    else {
        bShowAll = false;
        bEmitted = false;
    }

    filterSections[filterSectionDepth++] = {title, header, bEmitted, bShowAll, false};
    ImGui::PushID(title);
    return true;
}

void EndSection()
{
    ImGui::PopID();
    --filterSectionDepth;
}

void SubHeader(const char* title)
{
    if (!IsFiltering() || filterSectionDepth == 0) {
        if (!IsFiltering()) { ImGui::SeparatorText(title); }
        return;
    }

    FilterSection& section = filterSections[filterSectionDepth - 1];
    if (section.bShowAll) {
        ImGui::SeparatorText(title);
        return;
    }
    section.bSubHeaderShowAll = LabelPasses(title);
    if (section.bSubHeaderShowAll) {
        EmitPendingTitles();
        ImGui::SeparatorText(title);
    }
}

void SameLine()
{
    if (IsShowingAll()) { ImGui::SameLine(); }
}

bool Checkbox(const char* name, bool* v, const char* tooltip)
{
    if (!PassFilter(name)) { return false; }
    const bool changed = ImGui::Checkbox(name, v);
    DrawTooltip(tooltip);
    return changed;
}

bool Combo(const char* name, int* current, const char* const items[], int count, const char* tooltip)
{
    if (!PassFilter(name)) { return false; }
    const bool changed = ImGui::Combo(name, current, items, count);
    DrawTooltip(tooltip);
    return changed;
}

bool Button(const char* name, const char* tooltip)
{
    if (!PassFilter(name)) { return false; }
    const bool pressed = ImGui::Button(name);
    DrawTooltip(tooltip);
    return pressed;
}

bool ToggleButton(const char* name, bool bActive, const char* tooltip)
{
    if (!PassFilter(name)) { return false; }
    if (bActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.40f, 0.70f, 1.0f));
    }
    const bool pressed = ImGui::Button(name);
    if (bActive) { ImGui::PopStyleColor(3); }
    DrawTooltip(tooltip);
    return pressed;
}

bool SliderFloat(const char* name, float* v, float vMin, float vMax, const SliderOpts& opts)
{
    if (!PassFilter(name)) { return false; }

    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float resetWidth = ImGui::CalcTextSize("R").x + ImGui::GetStyle().FramePadding.x * 2.0f;

    bool changed = false;
    ImGui::PushID(name);

    ImGuiStorage* storage = opts.commitOnRelease ? ImGui::GetStateStorage() : nullptr;
    const ImGuiID scratchId = opts.commitOnRelease ? ImGui::GetID("##scratch") : 0;
    float scratch = opts.commitOnRelease ? storage->GetFloat(scratchId, *v) : *v;
    float* target = opts.commitOnRelease ? &scratch : v;

    bool committed = false;
    bool active = false;

    ImGui::SetNextItemWidth(SliderWidth(opts.reset, spacing, resetWidth));
    if (ImGui::SliderFloat("##s", target, vMin, vMax, "")) { changed = true; }
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    active |= ImGui::IsItemActive();
    DrawTooltip(opts.tooltip);

    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(kInputWidth);
    if (ImGui::InputFloat("##i", target, 0.0f, 0.0f, opts.format)) { changed = true; }
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    active |= ImGui::IsItemActive();

    if (opts.reset) {
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("R")) {
            *target = static_cast<float>(opts.resetTo);
            committed = true;
            changed = true;
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Reset to %g", opts.resetTo); }
    }

    ImGui::SameLine(0.0f, spacing);
    DrawName(name);
    DrawTooltip(opts.tooltip);

    ImGui::PopID();

    if (!opts.commitOnRelease) {
        return changed;
    }

    if (committed) {
        *v = ClampF(scratch, vMin, vMax);
        storage->SetFloat(scratchId, *v);
        return true;
    }

    storage->SetFloat(scratchId, active ? scratch : *v);
    return false;
}

bool SliderInt(const char* name, int* v, int vMin, int vMax, const SliderOpts& opts)
{
    if (!PassFilter(name)) { return false; }

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

constexpr float EV_LUMINANCE_REF = 0.125f;
constexpr float EV_ILLUMINANCE_REF = 2.5f;
constexpr float EV_MIN = -6.0f;
constexpr float EV_MAX = 30.0f;
constexpr float SIMPLE_LUMINANCE_REF = 65536.0f;
constexpr float SIMPLE_ILLUMINANCE_REF = 131072.0f;
constexpr float LUMEN_MAX = 1.0e7f;
constexpr float NITS_MAX = 1.0e9f;
constexpr float LUX_MAX = 1.0e6f;

static LightUnitDisplay lightUnitPreference = LightUnitDisplay::Lumens;

struct IntensityReference
{
    const char* label;
    float lo;
    float hi;
};

static constexpr IntensityReference LUMINANCE_REFERENCES[] = {
    {"Moonlit ground", 0.01f, 0.1f},
    {"Dim interior surface", 10.0f, 50.0f},
    {"Monitor / TV white", 100.0f, 400.0f},
    {"Overcast sky", 1000.0f, 2000.0f},
    {"Clear blue sky", 3000.0f, 8000.0f},
    {"Fluorescent tube surface", 10000.0f, 30000.0f},
    {"Sunlit white paper", 20000.0f, 30000.0f},
    {"Frosted bulb surface", 50000.0f, 100000.0f},
    {"Bare filament / LED die", 1.0e6f, 1.0e7f},
    {"Sun disk", 1.6e9f, 1.6e9f},
};

static constexpr IntensityReference ILLUMINANCE_REFERENCES[] = {
    {"Full moon", 0.05f, 0.3f},
    {"Street lighting", 5.0f, 30.0f},
    {"Living room", 50.0f, 150.0f},
    {"Office", 300.0f, 500.0f},
    {"Overcast daylight", 1000.0f, 10000.0f},
    {"Full daylight, no sun", 10000.0f, 25000.0f},
    {"Direct midday sun", 80000.0f, 120000.0f},
};

static constexpr IntensityReference FLUX_REFERENCES[] = {
    {"Candle", 12.0f, 12.0f},
    {"Phone torch", 50.0f, 100.0f},
    {"40W incandescent", 450.0f, 450.0f},
    {"60W incandescent", 800.0f, 800.0f},
    {"100W incandescent", 1600.0f, 1600.0f},
    {"Car headlight, low beam", 700.0f, 1200.0f},
    {"Ceiling panel 600x600", 3000.0f, 5000.0f},
    {"Street lamp", 8000.0f, 30000.0f},
    {"Stadium floodlight", 100000.0f, 500000.0f},
};

static void DrawReferenceValue(const IntensityReference& row, LightUnitDisplay unit, float evRef, float simpleRef, const char* unitLabel)
{
    if (unit == LightUnitDisplay::Simple) {
        if (row.lo == row.hi) { ImGui::Text("%.4g %s", row.lo / simpleRef, unitLabel); }
        else { ImGui::Text("%.4g - %.4g %s", row.lo / simpleRef, row.hi / simpleRef, unitLabel); }
        return;
    }

    if (unit == LightUnitDisplay::EV100) {
        if (row.lo == row.hi) { ImGui::Text("EV %.1f", std::log2(row.lo / evRef)); }
        else { ImGui::Text("EV %.1f - %.1f", std::log2(row.lo / evRef), std::log2(row.hi / evRef)); }
        return;
    }

    float div = 1.0f;
    const char* prefix = "";
    if (row.hi >= 1.0e6f) {
        div = 1.0e6f;
        prefix = "M";
    }
    else if (row.hi >= 1000.0f) {
        div = 1000.0f;
        prefix = "k";
    }

    if (row.lo == row.hi) { ImGui::Text("%.4g %s%s", row.lo / div, prefix, unitLabel); }
    else { ImGui::Text("%.4g - %.4g %s%s", row.lo / div, row.hi / div, prefix, unitLabel); }
}

static void DrawIntensityReferenceTooltip(LightUnitDisplay unit, bool bIlluminance, float evRef, float simpleRef, const char* unitLabel)
{
    const IntensityReference* rows = LUMINANCE_REFERENCES;
    size_t count = sizeof(LUMINANCE_REFERENCES) / sizeof(LUMINANCE_REFERENCES[0]);
    const char* title = "Common surface brightness";
    if (unit == LightUnitDisplay::Lumens) {
        rows = FLUX_REFERENCES;
        count = sizeof(FLUX_REFERENCES) / sizeof(FLUX_REFERENCES[0]);
        title = "Common total light output";
    }
    else if (bIlluminance) {
        rows = ILLUMINANCE_REFERENCES;
        count = sizeof(ILLUMINANCE_REFERENCES) / sizeof(ILLUMINANCE_REFERENCES[0]);
        title = "Common light arriving on a surface";
    }

    ImGui::BeginTooltip();
    ImGui::TextUnformatted(title);
    ImGui::Separator();
    if (ImGui::BeginTable("##intensityref", 2, ImGuiTableFlags_SizingFixedFit)) {
        for (size_t i = 0; i < count; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(rows[i].label);
            ImGui::TableSetColumnIndex(1);
            DrawReferenceValue(rows[i], unit, evRef, simpleRef, unitLabel);
        }
        ImGui::EndTable();
    }
    ImGui::EndTooltip();
}

bool DragLightIntensity(const char* name, float* value, const LightIntensityOpts& opts)
{
    const bool bLumensAvailable = opts.lumensPerNit > 0.0f && !opts.bIlluminance;
    const float evRef = opts.bIlluminance ? EV_ILLUMINANCE_REF : EV_LUMINANCE_REF;
    const float simpleRef = opts.bIlluminance ? SIMPLE_ILLUMINANCE_REF : SIMPLE_LUMINANCE_REF;
    const char* nativeLabel = opts.bIlluminance ? "lux" : "nits";
    const char* simpleLabel = opts.bIlluminance ? "x131k" : "x65k";

    LightUnitDisplay unit = lightUnitPreference;
    if (unit == LightUnitDisplay::Lumens && !bLumensAvailable) { unit = LightUnitDisplay::Native; }

    float display = *value;
    float dragMin = 0.0f;
    float dragMax = opts.bIlluminance ? LUX_MAX : NITS_MAX;
    float dragSpeed = 0.0f;
    const char* format = "%.0f";
    const char* unitLabel = nativeLabel;

    switch (unit) {
        case LightUnitDisplay::Lumens:
            display = *value * opts.lumensPerNit;
            dragMax = LUMEN_MAX;
            unitLabel = "lm";
            break;
        case LightUnitDisplay::Simple:
            display = *value / simpleRef;
            dragMax /= simpleRef;
            dragSpeed = display * 0.005f < 0.001f ? 0.001f : display * 0.005f;
            format = "%.3f";
            unitLabel = simpleLabel;
            break;
        case LightUnitDisplay::EV100:
            display = *value > 0.0f ? std::log2(*value / evRef) : EV_MIN;
            dragMin = EV_MIN;
            dragMax = EV_MAX;
            dragSpeed = 0.02f;
            format = "%.2f";
            unitLabel = "EV";
            break;
        default:
            break;
    }
    if (dragSpeed <= 0.0f) { dragSpeed = display * 0.005f < 1.0f ? 1.0f : display * 0.005f; }

    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float comboWidth = ImGui::CalcTextSize("x131k").x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float helpWidth = ImGui::CalcTextSize("(?)").x + spacing;
    float dragWidth = ImGui::CalcItemWidth() - comboWidth - helpWidth - spacing;
    if (dragWidth < kMinSliderWidth) { dragWidth = kMinSliderWidth; }

    ImGui::PushID(name);

    ImGui::SetNextItemWidth(dragWidth);
    const bool changed = ImGui::DragFloat("##v", &display, dragSpeed, dragMin, dragMax, format);
    DrawTooltip(opts.tooltip);

    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::BeginCombo("##unit", unitLabel, ImGuiComboFlags_HeightSmall)) {
        if (ImGui::Selectable(nativeLabel, unit == LightUnitDisplay::Native)) { lightUnitPreference = LightUnitDisplay::Native; }
        if (bLumensAvailable && ImGui::Selectable("lm", unit == LightUnitDisplay::Lumens)) { lightUnitPreference = LightUnitDisplay::Lumens; }
        if (ImGui::Selectable(simpleLabel, unit == LightUnitDisplay::Simple)) { lightUnitPreference = LightUnitDisplay::Simple; }
        if (ImGui::Selectable("EV", unit == LightUnitDisplay::EV100)) { lightUnitPreference = LightUnitDisplay::EV100; }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Display unit for every light intensity field"); }

    ImGui::SameLine(0.0f, spacing);
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) { DrawIntensityReferenceTooltip(unit, opts.bIlluminance, evRef, simpleRef, unitLabel); }

    ImGui::SameLine(0.0f, spacing);
    DrawName(name);
    if (ImGui::IsItemHovered()) {
        const float ev = *value > 0.0f ? std::log2(*value / evRef) : EV_MIN;
        if (bLumensAvailable) {
            ImGui::SetTooltip("%.0f %s\n%.0f lm\n%.3f %s\nEV %.2f", *value, nativeLabel, *value * opts.lumensPerNit, *value / simpleRef, simpleLabel, ev);
        }
        else {
            ImGui::SetTooltip("%.0f %s\n%.3f %s\nEV %.2f", *value, nativeLabel, *value / simpleRef, simpleLabel, ev);
        }
    }

    ImGui::PopID();

    if (!changed) { return false; }

    switch (unit) {
        case LightUnitDisplay::Lumens:
            *value = display / opts.lumensPerNit;
            break;
        case LightUnitDisplay::Simple:
            *value = display * simpleRef;
            break;
        case LightUnitDisplay::EV100:
            *value = evRef * std::exp2(display);
            break;
        default:
            *value = display;
            break;
    }
    if (*value < 0.0f) { *value = 0.0f; }
    return true;
}

bool SaveBar(const char* id, bool* autoSave)
{
    bool save = false;
    ImGui::PushID(id);
    ImGui::BeginDisabled(*autoSave);
    if (ImGui::Button("Save Config")) { save = true; }
    ImGui::EndDisabled();
    if (*autoSave && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("Auto-save is enabled"); }
    ImGui::SameLine();
    if (ImGui::Checkbox("Auto-save", autoSave)) { save = true; }
    ImGui::PopID();
    return save;
}

void DrawCategoryGroupTree(const char* tableId, const double* leafValues, const double* groupValues, double total, const char* fmt)
{
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable(tableId, 3, flags)) { return; }
    ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 80.f);
    ImGui::TableSetupColumn("% of Total", ImGuiTableColumnFlags_WidthFixed, 80.f);
    ImGui::TableHeadersRow();

    auto Row = [&](bool bTree, const char* name, double value, bool* pOpen) -> bool {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool bOpen = false;
        if (bTree) {
            bOpen = ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_SpanFullWidth);
        }
        else {
            ImGui::Indent();
            ImGui::TextUnformatted(name);
            ImGui::Unindent();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::Text(fmt, value);
        ImGui::TableSetColumnIndex(2);
        if (total > 0.0) { ImGui::Text("%.1f%%", 100.0 * value / total); }
        else { ImGui::TextUnformatted("-"); }
        if (pOpen) { *pOpen = bOpen; }
        return bOpen;
    };

    for (uint32_t group = 0; group < Render::RENDER_CATEGORY_GROUP_COUNT; ++group) {
        if (groupValues[group] <= 0.0) { continue; }

        // Groups with exactly one contributing leaf render as a flat row (no expand arrow needed).
        uint32_t leafCount = 0;
        for (uint32_t bit = 0; bit < Render::RENDER_CATEGORY_BIT_COUNT; ++bit) {
            if (static_cast<uint32_t>(Render::RENDER_CATEGORY_GROUP_OF[bit]) == group && leafValues[bit] > 0.0) {
                ++leafCount;
            }
        }

        if (leafCount <= 1) {
            Row(false, Render::RENDER_CATEGORY_GROUP_NAMES[group], groupValues[group], nullptr);
            continue;
        }

        bool bOpen = false;
        Row(true, Render::RENDER_CATEGORY_GROUP_NAMES[group], groupValues[group], &bOpen);
        if (bOpen) {
            for (uint32_t bit = 0; bit < Render::RENDER_CATEGORY_BIT_COUNT; ++bit) {
                if (static_cast<uint32_t>(Render::RENDER_CATEGORY_GROUP_OF[bit]) == group && leafValues[bit] > 0.0) {
                    ImGui::Indent();
                    Row(false, Render::RENDER_CATEGORY_NAMES[bit], leafValues[bit], nullptr);
                    ImGui::Unindent();
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndTable();
}
}
