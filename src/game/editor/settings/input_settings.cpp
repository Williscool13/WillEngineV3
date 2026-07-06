//
// Created by William on 2026-07-06.
//

#include "input_settings.h"

#include "imgui.h"

#include "core/input/input_names.h"
#include "engine/engine_api.h"
#include "engine/input/input_rebinding.h"
#include "game/input/game_actions.h"

namespace Game
{
struct DisplayedAction
{
    Engine::ActionHandle action;
    const char* name{};
};

static const DisplayedAction DISPLAYED_ACTIONS[] = {
    {Actions::ACTION_MOVE, "Move"},
    {Actions::ACTION_JUMP, "Jump"},
};

static const char* BindingSourceLabel(const Engine::BindingSource& source)
{
    switch (source.type) {
        case Engine::BindingSourceType::Key: return Core::GetKeyName(source.key);
        case Engine::BindingSourceType::MouseButton: return Core::GetMouseButtonName(source.mouseButton);
        case Engine::BindingSourceType::GamepadButton: return Core::GetGamepadButtonName(source.gamepadButton);
        case Engine::BindingSourceType::GamepadAxis: return "Gamepad Axis";
    }
    return "?";
}

void DrawInputBindingsWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (ImGui::Begin("Input Bindings")) {
        for (const DisplayedAction& displayed : DISPLAYED_ACTIONS) {
            ImGui::SeparatorText(displayed.name);

            const Core::InlineVector<size_t, 8> rows = Engine::EnumerateBindingRows(state->input, displayed.action);
            for (size_t idx = 0; idx < rows.Size(); ++idx) {
                const size_t row = rows[idx];
                const Engine::ActionBinding& binding = state->input.bindings[row];
                ImGui::PushID(static_cast<int>(row));

                if (binding.shape != Engine::BindingShape::Discrete) {
                    ImGui::TextDisabled("%s", binding.shape == Engine::BindingShape::Axis2DComposite ? "WASD (keyboard, not rebindable yet)" : "Left Stick (gamepad, not rebindable yet)");
                    ImGui::PopID();
                    continue;
                }

                const bool isCapturingThisRow = state->input.bCaptureActive && state->input.captureTargetBindingRow == row;
                if (isCapturingThisRow) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Press any key/button... (ESC to cancel)");
                }
                else {
                    ImGui::Text("%s", BindingSourceLabel(binding.source));
                    ImGui::SameLine();
                    ImGui::BeginDisabled(state->input.bCaptureActive);
                    if (ImGui::Button("Rebind")) {
                        Engine::BeginCapture(state->input, row);
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Reset")) {
                        Engine::ResetBindingToDefault(state->input, row);
                    }
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}
} // Game
