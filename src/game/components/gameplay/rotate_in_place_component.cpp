//
// Created by William on 2026-06-16.
//

#include "rotate_in_place_component.h"

#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>
#include <imgui.h>

#include "render/interface/render_interface.h"
#include "engine/component_registry.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "game/component-registry/component_editor.h"
#include "game/components/core_components.h"

namespace Game::Component
{
void RotateInPlaceComponent::Serialize(const RotateInPlaceComponent& comp, Engine::TextWriter& w)
{
    static const RotateInPlaceComponent DEF{};
    w.KeyOpt("axis", comp.axis, DEF.axis);
    w.KeyOpt("speedDegrees", comp.speedDegrees, DEF.speedDegrees);
    w.KeyOpt("bWorldSpace", comp.bWorldSpace, DEF.bWorldSpace);
}

void RotateInPlaceComponent::Deserialize(RotateInPlaceComponent& comp, const Engine::TextReader& r)
{
    comp.axis = r.Vec3("axis", comp.axis);
    comp.speedDegrees = r.Float("speedDegrees", comp.speedDegrees);
    comp.bWorldSpace = r.Bool("bWorldSpace", comp.bWorldSpace);
}

Engine::ComponentEditorResult RotateInPlaceComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& component = registry.get<RotateInPlaceComponent>(entity);

    bool open = ImGui::CollapsingHeader("Rotate In Place", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleterotateinplace");
    ImGui::PopStyleColor();

    bool modified = false;
    if (open) {
        modified |= ImGui::DragFloat3("Axis", &component.axis.x, 0.01f);
        modified |= ImGui::DragFloat("Speed", &component.speedDegrees, 1.0f, -3600.0f, 3600.0f, "%.1f deg/s");
        modified |= ImGui::Checkbox("World Space", &component.bWorldSpace);

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

    return {.bRequestRemoval = remove, .bModified = modified};
}
} // Game::Component
