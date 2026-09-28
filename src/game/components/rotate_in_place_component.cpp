//
// Created by William on 2026-06-16.
//

#include "rotate_in_place_component.h"

#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>
#include <imgui.h>

#include "render/interface/render_interface.h"
#include "engine/component_registry.h"
#include "engine/components/component_editor.h"
#include "engine/components/core_components.h"
#include "game/fwd_components.h"

namespace Game::Component
{
Engine::ComponentEditorResult RotateInPlaceComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    const auto& component = edit.Get<RotateInPlaceComponent>();

    bool open = ImGui::CollapsingHeader("Rotate In Place", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleterotateinplace");
    ImGui::PopStyleColor();

    if (open) {
        Engine::EditWidgets::DragFloat3(edit, "Axis", &RotateInPlaceComponent::axis, 0.01f);
        Engine::EditWidgets::DragFloat(edit, "Speed", &RotateInPlaceComponent::speedDegrees, 1.0f, -3600.0f, 3600.0f, "%.1f deg/s");
        Engine::EditWidgets::Checkbox(edit, "World Space", &RotateInPlaceComponent::bWorldSpace);

        auto* transform = registry.try_get<TransformComponent>(entity);
        if (transform) {
            constexpr glm::vec4 kAxisColor{0.3f, 0.7f, 1.0f, 1.0f};
            constexpr glm::vec4 kCircleColor{1.0f, 0.8f, 0.2f, 1.0f};
            constexpr float kAxisLength = 1.0f;
            constexpr float kCircleRadius = 0.6f;
            constexpr int kCircleSegments = 32;

            const glm::vec3 center = transform->translation;
            const float axisLen = glm::length(component.axis);
            const glm::vec3 localAxis = axisLen > 1e-5f ? component.axis / axisLen : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 worldAxis = component.bWorldSpace ? localAxis : transform->rotation * localAxis;

            DEBUG_ADD_ARROW(viewFamily.debugArrows, Core::DebugArrow{
                .start = center - worldAxis * kAxisLength,
                .end = center + worldAxis * kAxisLength,
                .color = kAxisColor,
            });

            const glm::vec3 ref = glm::abs(worldAxis.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 u = glm::normalize(glm::cross(ref, worldAxis));
            const glm::vec3 v = glm::normalize(glm::cross(worldAxis, u));

            glm::vec3 prev = center + u * kCircleRadius;
            for (int i = 1; i <= kCircleSegments; i++) {
                const float a = (static_cast<float>(i) / static_cast<float>(kCircleSegments)) * glm::two_pi<float>();
                const glm::vec3 cur = center + (glm::cos(a) * u + glm::sin(a) * v) * kCircleRadius;
                DEBUG_ADD_LINE(viewFamily.debugLines, Core::DebugLine{.start = prev, .end = cur, .color = kCircleColor, .width = 0.03f});
                prev = cur;
            }
        }
    }

    return {.bRequestRemoval = remove};
}
} // Game::Component
