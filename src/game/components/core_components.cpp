//
// Created by William on 2026-02-08.
//

#include "core_components.h"

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <ImGuizmo.h>

#include "engine/engine_api.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

#include "game/component-registry/component_editor.h"
#include "game/component-registry/editor_gizmo_helpers.h"
#include "game/components/render_components.h"

void Game::Component::TransformComponent::Serialize(const TransformComponent& comp, Engine::TextWriter& w)
{
    static const TransformComponent DEF{};
    w.KeyOpt("translation", comp.translation, DEF.translation);
    w.KeyOpt("rotation", comp.rotation, DEF.rotation);
    w.KeyOpt("scale", comp.scale, DEF.scale);
}

void Game::Component::TransformComponent::Deserialize(TransformComponent& comp, const Engine::TextReader& r)
{
    comp.translation = r.Vec3("translation", comp.translation);
    comp.rotation = r.Quat("rotation", comp.rotation);
    comp.scale = r.Vec3("scale", comp.scale);
}

void Game::Component::TransformComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
    registry.emplace_or_replace<WorldTransformComponent>(entity);
}

void Game::Component::TransformComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    registry.remove<MultiframeDirtyTransformComponent>(entity);
    registry.remove<WorldTransformComponent>(entity);
}

Transform Game::Component::ComputeWorldTransform(const entt::registry& registry, entt::entity entity)
{
    const auto& local = registry.get<TransformComponent>(entity);
    const auto* node = registry.try_get<HierarchyComponent>(entity);
    if (!node || node->parent == entt::null || !registry.valid(node->parent)) {
        return {local.translation, local.rotation, local.scale};
    }
    return ComposeWorldTransform(ComputeWorldTransform(registry, node->parent), local);
}

void Game::Component::HierarchyComponent::Serialize(const HierarchyComponent& comp, Engine::TextWriter& w)
{
    w.KeyOpt("parentStableId", comp.parentStableId.id, uint64_t{0});
}

void Game::Component::HierarchyComponent::Deserialize(HierarchyComponent& comp, const Engine::TextReader& r)
{
    comp.parentStableId = StringID(r.U64("parentStableId", comp.parentStableId.id));
    comp.parent = entt::null;
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

    if (!open) { return {.bRequestRemoval = remove}; }

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

        c |= drawField(idX, v + 0, Editor::COLOR_AXIS_X);
        ImGui::SameLine(0, innerSpacing);
        c |= drawField(idY, v + 1, Editor::COLOR_AXIS_Y);
        ImGui::SameLine(0, innerSpacing);
        c |= drawField(idZ, v + 2, Editor::COLOR_AXIS_Z);
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

    return {.bRequestRemoval = remove, .bModified = dirty};
}
}
