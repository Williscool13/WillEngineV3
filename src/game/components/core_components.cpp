//
// Created by William on 2026-02-08.
//

#include "core_components.h"

#include <glm/gtc/type_ptr.hpp>
#include <json/nlohmann/json.hpp>
#include <imgui.h>
#include <ImGuizmo.h>

#include "engine/engine_api.h"

#include "game/component-registry/component_editor.h"
#include "game/component-registry/editor_gizmo_helpers.h"

void Game::Component::TransformComponent::Serialize(const TransformComponent& comp, nlohmann::json& json)
{
    json["translation"] = {comp.translation.x, comp.translation.y, comp.translation.z};
    json["rotation"] = {comp.rotation.w, comp.rotation.x, comp.rotation.y, comp.rotation.z};
    json["scale"] = {comp.scale.x, comp.scale.y, comp.scale.z};
}

void Game::Component::TransformComponent::Deserialize(TransformComponent& comp, const nlohmann::json& json)
{
    const auto& t = json["translation"];
    comp.translation = glm::vec3(t[0].get<float>(), t[1].get<float>(), t[2].get<float>());

    // glm::quat constructor order: (w, x, y, z)
    const auto& r = json["rotation"];
    comp.rotation = glm::quat(r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>());

    const auto& s = json["scale"];
    comp.scale = glm::vec3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>());
}

namespace Game
{
Engine::ComponentEditorResult Component::TransformComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry,
                                                                        entt::entity entity, const char* name)
{
    auto& component = registry.get<Component::TransformComponent>(entity);
    bool open = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletetransform");
    ImGui::PopStyleColor();

    if (!open) { return {.requestRemoval = remove}; }

    bool dirty = false;
    Engine::EngineState* state = registry.ctx().get<Engine::EngineState*>();

    const float innerSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float outerSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float frameRounding = ImGui::GetStyle().FrameRounding;
    const float labelColW = ImGui::CalcTextSize("Translation").x + outerSpacing * 3.0f;
    const float fieldW = (ImGui::GetContentRegionAvail().x - labelColW - innerSpacing * 2.0f) / 3.0f;
    const float fieldH = ImGui::GetFrameHeight();

    constexpr float stripW = 4.0f;

    auto drawXYZ = [&](const char* idX, const char* idY, const char* idZ, float* v, float speed) -> bool {
        bool c = false;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        auto drawField = [&](const char* id, float* val, ImU32 strip) -> bool {
            ImGui::SetNextItemWidth(fieldW);
            bool changed = ImGui::DragFloat(id, val, speed, 0, 0, "%.1f");
            ImVec2 p = ImGui::GetItemRectMin();
            dl->AddRectFilled(p, {p.x + stripW, p.y + fieldH}, strip, frameRounding, ImDrawFlags_RoundCornersLeft);
            return changed;
        };

        c |= drawField(idX, v + 0, Editor::ColorAxisX);
        ImGui::SameLine(0, innerSpacing);
        c |= drawField(idY, v + 1, Editor::ColorAxisY);
        ImGui::SameLine(0, innerSpacing);
        c |= drawField(idZ, v + 2, Editor::ColorAxisZ);
        return c;
    };

    // Translation
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Translation");
    ImGui::SameLine(labelColW);
    dirty |= drawXYZ("##tx", "##ty", "##tz", &component.translation.x, 0.1f);

    // Rotation
    glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(component.rotation));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Rotation");
    ImGui::SameLine(labelColW);
    if (drawXYZ("##rx", "##ry", "##rz", &eulerDegrees.x, 0.5f)) {
        component.rotation = glm::quat(glm::radians(eulerDegrees));
        dirty = true;
    }

    // Scale
    glm::vec3 prevScale = component.scale;
    if (ImGui::Checkbox("##uniform", &state->editor.bUniformScaleMode)) {
        if (state->editor.bUniformScaleMode) {
            float uniform = glm::max(glm::max(component.scale.x, component.scale.y), component.scale.z);
            component.scale = glm::vec3(uniform);
        }
        dirty = true;
    }
    ImGui::SameLine(0, outerSpacing);
    ImGui::TextUnformatted("Scale");
    ImGui::SameLine(labelColW);
    dirty |= drawXYZ("##sx", "##sy", "##sz", &component.scale.x, 0.01f);

    if (dirty && state->editor.bUniformScaleMode) {
        if (component.scale.x != prevScale.x) {
            component.scale = glm::vec3(component.scale.x);
        }
        else if (component.scale.y != prevScale.y) {
            component.scale = glm::vec3(component.scale.y);
        }
        else if (component.scale.z != prevScale.z) {
            component.scale = glm::vec3(component.scale.z);
        }
    }

    if (dirty) {
        registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
    }

    return {.requestRemoval = remove};
}
}
