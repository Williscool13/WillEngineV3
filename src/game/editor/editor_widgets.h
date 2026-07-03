//
// Created by William on 2026-06-13.
//

#ifndef WILL_ENGINE_EDITOR_WIDGETS_H
#define WILL_ENGINE_EDITOR_WIDGETS_H

namespace Game::Widgets
{
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

/**
 * Per-tab "Save Config" button + "Auto-save" checkbox bound to autoSave; returns true when a save should happen now (Save pressed, or auto-save was just toggled on).
 * The caller still triggers its own save whenever a value changes while autoSave is set.
 */
bool SaveBar(const char* id, bool* autoSave);
}

#endif //WILL_ENGINE_EDITOR_WIDGETS_H
