//
// Created by William on 2026-06-13.
//

#ifndef WILL_ENGINE_EDITOR_WIDGETS_H
#define WILL_ENGINE_EDITOR_WIDGETS_H

namespace Game::Widgets
{
/**
 * Optional styling for the labelled slider widgets.
 */
struct SliderOpts
{
    /** printf format for the value shown in the input field. */
    const char* format = "%.3f";
    /** Hover tooltip shown over the slider, input field, and name. Null for none. */
    const char* tooltip = nullptr;
    /** When true, draws a reset button (between the input field and the name) that writes resetTo. */
    bool reset = false;
    /** Value applied when the reset button is pressed. */
    double resetTo = 0.0;
};

/**
 * Slider laid out as [slider (no value text)] [input field] [reset?] [name].
 * The name follows the ImGui label convention: text after "##" is used only for the widget id and is not displayed, so labels can repeat across sections.
 * @return True if the value changed this frame.
 */
bool SliderFloat(const char* name, float* v, float vMin, float vMax, const SliderOpts& opts = {});
bool SliderInt(const char* name, int* v, int vMin, int vMax, const SliderOpts& opts = {});
}

#endif //WILL_ENGINE_EDITOR_WIDGETS_H
