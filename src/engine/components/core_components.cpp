//
// Created by William on 2026-02-08.
//

#include "core_components.h"

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <ImGuizmo.h>

#include "engine/engine_api.h"

#include "engine/components/component_editor.h"
#include "engine/editor/editor_gizmo_helpers.h"
#include "engine/editor/editor_multi_edit.h"
#include "engine/components/render_components.h"


void Engine::Component::TransformComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
    registry.emplace_or_replace<WorldTransformComponent>(entity);
}

void Engine::Component::TransformComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    registry.remove<MultiframeDirtyComponent>(entity);
    registry.remove<WorldTransformComponent>(entity);
}

Transform Engine::Component::ComputeWorldTransform(const entt::registry& registry, entt::entity entity)
{
    const auto& local = registry.get<TransformComponent>(entity);
    const auto* node = registry.try_get<HierarchyComponent>(entity);
    if (!node || node->parent == entt::null || !registry.valid(node->parent)) {
        return {local.translation, local.rotation, local.scale};
    }
    return ComposeWorldTransform(ComputeWorldTransform(registry, node->parent), local);
}

void Engine::Component::TransformComponent::OnEditPreview(entt::registry& registry, entt::entity entity)
{
    registry.emplace_or_replace<DirtyTransformTag>(entity);
}

void Engine::Component::TransformComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    registry.emplace_or_replace<DirtyTransformTag>(entity);
}

namespace Engine
{
static ImGuiID gTransformExprField = 0;
static bool bTransformExprFocus = false;
static char gTransformExprBuf[64]{};

Engine::ComponentEditorResult Component::TransformComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    TransformComponent component = edit.Get<TransformComponent>();
    bool open = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    const float headerAvail = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(headerAvail - 10.f - ImGui::CalcTextSize("(?)").x - ImGui::GetStyle().ItemSpacing.x);
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Double-click or Ctrl+click a field to type an expression:\n"
                          "  5          set to 5\n"
                          "  x+1        current value plus 1\n"
                          "  *2  or  /2   scale the current value\n"
                          "  x+R(-1,1)  random float in [a,b)\n"
                          "Terms: number, x (current), R(a,b); at most one + - * /");
    }
    ImGui::SameLine(headerAvail - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletetransform");
    ImGui::PopStyleColor();

    if (!open) { return {.bRequestRemoval = remove}; }

    bool dirty = false;
    bool bReleased = false;
    Engine::EngineState* state = edit.State();

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
            const ImGuiID fieldId = ImGui::GetID(id);
            bool changed = false;
            if (gTransformExprField == fieldId) {
                if (bTransformExprFocus) {
                    ImGui::SetKeyboardFocusHere();
                    bTransformExprFocus = false;
                }
                ImGui::PushID(id);
                const bool bEnter = ImGui::InputText("##expr", gTransformExprBuf, sizeof(gTransformExprBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                const bool bDone = bEnter || ImGui::IsItemDeactivated();
                ImGui::PopID();
                if (bDone) {
                    float v;
                    if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && MultiEdit::EvaluateFloatField(gTransformExprBuf, *val, 0, state->rng, v) && v != *val) {
                        *val = v;
                        changed = true;
                        bReleased = true;
                    }
                    gTransformExprField = 0;
                }
            }
            else {
                changed = ImGui::DragFloat(id, val, speed, 0, 0, "%.1f", ImGuiSliderFlags_NoInput);
                bReleased |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::IsItemHovered() && (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || (ImGui::GetIO().KeyCtrl && ImGui::IsItemClicked(ImGuiMouseButton_Left)))) {
                    gTransformExprField = fieldId;
                    bTransformExprFocus = true;
                    const auto text = Core::InlineString<64>::Format("%g", *val);
                    memcpy(gTransformExprBuf, text.c_str(), text.Size() + 1);
                }
            }
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
    const bool bHintValid = state->editor.rotationHintEntity == edit.Primary() && glm::abs(glm::dot(state->editor.rotationHint, component.rotation)) > 0.99999f;
    glm::vec3 eulerDegrees = bHintValid ? state->editor.rotationHintDegrees : glm::degrees(glm::eulerAngles(component.rotation));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Rotation");
    ImGui::SameLine(labelColW);
    if (drawXYZ("##rx", "##ry", "##rz", &eulerDegrees.x, 0.5f)) {
        component.rotation = glm::quat(glm::radians(eulerDegrees));
        state->editor.rotationHintEntity = edit.Primary();
        state->editor.rotationHint = component.rotation;
        state->editor.rotationHintDegrees = eulerDegrees;
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
        edit.Preview<TransformComponent>([&component](TransformComponent& c) {
            c.translation = component.translation;
            c.rotation = component.rotation;
            c.scale = component.scale;
        });
    }
    if (bReleased) {
        edit.Commit<TransformComponent>();
    }

    return {.bRequestRemoval = remove};
}
}
