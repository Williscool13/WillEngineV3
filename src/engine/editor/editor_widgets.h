//
// Created by William on 2026-06-13.
//

#ifndef WILL_ENGINE_EDITOR_WIDGETS_H
#define WILL_ENGINE_EDITOR_WIDGETS_H

#include <cstdint>

struct ImGuiTextFilter;

namespace Engine::Widgets
{
/** Widgets below hide when their label misses the filter; a matching section or sub-header title shows its whole block. */
void BeginFilter(const ImGuiTextFilter* filter);
void EndFilter();

/** Guard raw ImGui content with this. */
bool IsShowingAll();

/** Emits pending section titles on a match. */
bool PassFilter(const char* label);

enum class SectionAction : uint8_t
{
    None = 0,
    Save,
    Revert,
};

struct SectionHeader
{
    bool bDirty = false;
    bool bSaveRevert = false;
    bool bCanSave = false;
    bool bCanRevert = false;
    const char* disabledTooltip = nullptr;
    /** Checkbox drawn before the title. */
    bool* enabled = nullptr;
    bool bEnabledChanged = false;
    SectionAction action = SectionAction::None;
};

/** Call EndSection only when this returns true; header->action and bEnabledChanged are set even while collapsed. */
bool BeginSection(const char* title, SectionHeader* header = nullptr);
void EndSection();

void SubHeader(const char* title);

/** No-op while filtering, so surviving widgets don't pair up with unrelated ones. */
void SameLine();

bool Checkbox(const char* name, bool* v, const char* tooltip = nullptr);

bool Combo(const char* name, int* current, const char* const items[], int count, const char* tooltip = nullptr);

bool Button(const char* name, const char* tooltip = nullptr);

/** Button highlighted while @p bActive. */
bool ToggleButton(const char* name, bool bActive, const char* tooltip = nullptr);

struct SliderOpts
{
    const char* format = "%.3f";
    const char* tooltip = nullptr;
    bool reset = false;
    double resetTo = 0.0;
    bool commitOnRelease = false;
};

/**
 * Slider laid out as [slider (no value text)] [input field] [reset?] [name]; returns true if the value changed this frame.
 * The name follows the ImGui label convention so text after "##" is id-only and not shown, letting labels repeat across sections.
 */
bool SliderFloat(const char* name, float* v, float vMin, float vMax, const SliderOpts& opts = {});

bool SliderInt(const char* name, int* v, int vMin, int vMax, const SliderOpts& opts = {});

enum class LightUnitDisplay : uint8_t
{
    Native = 0,
    Lumens,
    Simple,
    EV100,
};

struct LightIntensityOpts
{
    float lumensPerNit = 0.0f;
    bool bIlluminance = false;
    const char* tooltip = nullptr;
};

/**
 * Intensity drag laid out as [drag] [unit combo] [reference hint] [name]. The stored value stays in nits, or lux when bIlluminance;
 * only the display converts. The selected unit is an editor-wide preference, so changing it on one light changes every intensity field.
 * @param name label, following the ImGui "##" convention
 * @param value stored nits or lux, written back in stored units
 * @param opts lumensPerNit is this emitter's world-space lumens per stored nit, 0 to drop lumens from the unit list
 * @return true if the stored value changed this frame
 */
bool DragLightIntensity(const char* name, float* value, const LightIntensityOpts& opts = {});

/**
 * Per-tab "Save Config" button + "Auto-save" checkbox bound to autoSave; returns true when a save should happen now (Save pressed, or auto-save was just toggled on).
 * The caller still triggers its own save whenever a value changes while autoSave is set.
 */
bool SaveBar(const char* id, bool* autoSave);

/**
 * Shared two-level tree renderer for the VRAM report and the GPU pass-timing report: a row per RenderCategoryGroup with its total, expandable to the RenderCategory leaves rolled into it.
 * leafValues is indexed by RenderCategory bit, groupValues by RenderCategoryGroup.
 */
void DrawCategoryGroupTree(const char* tableId, const double* leafValues, const double* groupValues, double total, const char* fmt);
}

#endif //WILL_ENGINE_EDITOR_WIDGETS_H
