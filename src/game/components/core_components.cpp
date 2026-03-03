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
ComponentEditorResult DrawComponentEditor<Component::TransformComponent>(Component::TransformComponent& component, const Core::ViewFamily& viewFamily, entt::registry& registry,
                                                        entt::entity entity)
{
    ImGui::Separator();

    bool dirty = false;
    dirty |= ImGui::DragFloat3("Translation", &component.translation.x, 0.1f);
    glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(component.rotation));
    if (ImGui::DragFloat3("Rotation", &eulerDegrees.x, 0.5f)) {
        component.rotation = glm::quat(glm::radians(eulerDegrees));
        dirty = true;
    }
    Engine::GameState* state = registry.ctx().get<Engine::GameState*>();
    glm::vec3 prevScale = component.scale;

    ImGui::Checkbox("##uniform", &state->bUniformScaleMode);
    ImGui::SameLine();
    dirty |= ImGui::DragFloat3("Scale", &component.scale.x, 0.01f);

    if (dirty && state->bUniformScaleMode) {
        if (component.scale.x != prevScale.x)
            component.scale = glm::vec3(component.scale.x);
        else if (component.scale.y != prevScale.y)
            component.scale = glm::vec3(component.scale.y);
        else if (component.scale.z != prevScale.z)
            component.scale = glm::vec3(component.scale.z);
    }

    glm::mat4 view = viewFamily.mainView.currentViewData.view;
    glm::mat4 proj = viewFamily.mainView.currentViewData.proj;
    glm::mat4 model = Component::GetMatrix(component);
    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        state->currentGizmoOperation,
        state->currentGizmoMode,
        glm::value_ptr(model)
    );

    if (ImGuizmo::IsUsing()) {
        float translation[3], rotation[3], scale[3];
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), translation, rotation, scale);
        component.translation = glm::vec3(translation[0], translation[1], translation[2]);
        component.rotation = glm::quat(glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2])));
        if (state->bUniformScaleMode) {
            float uniformValue = (scale[0] + scale[1] + scale[2]) / 3.0f;
            component.scale = glm::vec3(uniformValue);
        } else {
            component.scale = glm::vec3(scale[0], scale[1], scale[2]);
        }
        dirty = true;
    }

    if (dirty) {
        registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
    }

    return {};
}
}