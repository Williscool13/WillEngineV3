//
// Created by William on 2026-07-06.
//

#include "input_settings.h"

#include "imgui.h"

#include "core/input/input_names.h"
#include "engine/engine_api.h"
#include "engine/input/input_rebinding.h"
#include "engine/input_config.h"
#include "engine/profiles/profile_library.h"
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

static void DrawInputProfiles(Engine::EngineState* state)
{
    Engine::ProjectConfig& cfg = state->projectConfig;
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Profile##inputprofile", cfg.activeInputProfile.IsEmpty() ? "Default" : cfg.activeInputProfile.c_str())) {
        if (ImGui::Selectable("Default", cfg.activeInputProfile.IsEmpty())) {
            cfg.activeInputProfile = Core::InlineString<64>();
            Engine::LoadAndApplyInputConfig(state->input, cfg);
            Engine::WriteProjectConfig(cfg);
        }

        Engine::Profiles::ProfileName names[Engine::Profiles::MAX_PROFILES];
        const uint32_t count = Engine::Profiles::ListInputProfiles(names, Engine::Profiles::MAX_PROFILES);
        for (uint32_t i = 0; i < count; ++i) {
            if (ImGui::Selectable(names[i].c_str(), cfg.activeInputProfile == names[i])) {
                cfg.activeInputProfile = names[i];
                Engine::InputConfig loaded{};
                Engine::Profiles::LoadInputProfile(names[i].c_str(), loaded);
                Engine::ApplyInputOverrides(state->input, loaded);
                Engine::WriteProjectConfig(cfg);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(cfg.activeInputProfile.IsEmpty());
    if (ImGui::Button("Delete##inputprofile")) {
        Engine::Profiles::DeleteInputProfile(cfg.activeInputProfile.c_str());
        cfg.activeInputProfile = Core::InlineString<64>();
        Engine::SaveInputConfig(state->input, cfg);
        Engine::WriteProjectConfig(cfg);
    }
    ImGui::EndDisabled();

    static char inputNewName[64] = "";
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("##inputnewname", inputNewName, sizeof(inputNewName));
    ImGui::SameLine();
    if (ImGui::Button("Save As##inputprofile") && inputNewName[0] != '\0') {
        Engine::Profiles::SaveInputProfile(inputNewName, Engine::BuildInputConfigFromState(state->input));
        cfg.activeInputProfile = Core::InlineString<64>(inputNewName);
        Engine::WriteProjectConfig(cfg);
        inputNewName[0] = '\0';
    }
}

void DrawInputBindingsWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (ImGui::Begin("Input Bindings")) {
        DrawInputProfiles(state);
        ImGui::Separator();

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
