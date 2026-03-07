//
// Created by William on 2026-02-08.
//

#include "core_components.h"

#include <glm/gtc/type_ptr.hpp>
#include <json/nlohmann/json.hpp>
#include <imgui.h>
#include <ImGuizmo.h>

#include "component_serialization.h"
#include "engine/engine_api.h"
#include "game/systems/editor_systems.h"

namespace Game
{
template<>
void SerializeComponent<Component::TransformComponent>(const Component::TransformComponent& comp, nlohmann::json& json)
{
    json["translation"] = {comp.translation.x, comp.translation.y, comp.translation.z};
    json["rotation"]    = {comp.rotation.w, comp.rotation.x, comp.rotation.y, comp.rotation.z};
    json["scale"]       = {comp.scale.x, comp.scale.y, comp.scale.z};
}

template<>
void DeserializeComponent<Component::TransformComponent>(Component::TransformComponent& comp, const nlohmann::json& json)
{
    const auto& t = json["translation"];
    comp.translation = glm::vec3(t[0].get<float>(), t[1].get<float>(), t[2].get<float>());

    // glm::quat constructor order: (w, x, y, z)
    const auto& r = json["rotation"];
    comp.rotation = glm::quat(r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>());

    const auto& s = json["scale"];
    comp.scale = glm::vec3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>());
}
}

namespace Game
{
template<>
ComponentEditorResult DrawComponentEditor<Component::TransformComponent>(Component::TransformComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry,
                                                        entt::entity entity)
{
    bool open = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletetransform");
    ImGui::PopStyleColor();

    if (!open) { return {.requestRemoval = remove}; }

    bool dirty = false;
    dirty |= ImGui::DragFloat3("Translation", &component.translation.x, 0.1f);
    glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(component.rotation));
    if (ImGui::DragFloat3("Rotation", &eulerDegrees.x, 0.5f)) {
        component.rotation = glm::quat(glm::radians(eulerDegrees));
        dirty = true;
    }
    Engine::GameState* state = registry.ctx().get<Engine::GameState*>();
    glm::vec3 prevScale = component.scale;

    if (ImGui::Checkbox("##uniform", &state->bUniformScaleMode)) {
        if (state->bUniformScaleMode) {
            float uniform = glm::max(glm::max(component.scale.x, component.scale.y), component.scale.z);
            component.scale = glm::vec3(uniform);
        }
        dirty = true;
    }
    ImGui::SameLine();
    dirty |= ImGui::DragFloat3("Scale", &component.scale.x, 0.01f);

    if (dirty && state->bUniformScaleMode) {
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