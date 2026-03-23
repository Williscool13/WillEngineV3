//
// Created by William on 2026-03-21.
//

#include "spline_mesh_component.h"

#include <glm/gtc/type_ptr.hpp>
#include <json/nlohmann/json.hpp>

#include "static_mesh_component.h"
#include "core/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/components/component_types.h"
#include "game/components/core_components.h"

namespace Game::Component
{
static Engine::SplineParams ToSplineParams(const SplineMeshComponent& component)
{
    Engine::SplineParams params{};
    params.controlPoints = component.controlPoints;
    params.radius = component.radius;
    params.rollAngle = component.rollAngle;
    params.sides = component.sides;
    params.segmentsPerSpan = component.segmentsPerSpan;
    params.bCaps = component.bCaps;
    params.bDualPath = component.bDualPath;
    params.dualPathSpacing = component.dualPathSpacing;
    params.bCrossPlanks = component.bCrossPlanks;
    params.crossPlankInterval = component.crossPlankInterval;
    params.crossPlankHeight = component.crossPlankHeight;
    return params;
}

void SplineMeshComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<SplineMeshComponent>(entity);
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto* state = registry.ctx().get<Engine::GameState*>();

    if (component.controlPoints.IsEmpty()) {
        component.controlPoints.PushBack({0,0,0,0});
        component.controlPoints.PushBack({0,0,1,0});
        component.controlPoints.PushBack({0,0,2,0});
        component.controlPoints.PushBack({0,0,3,0});
    }

    auto& runtime = registry.get_or_emplace<MeshRuntime>(entity);
    runtime.modelHandle = ctx->assetManager->LoadSplineModel(ToSplineParams(component));
    registry.emplace_or_replace<SplineMeshLoadingTag>(entity);
    state->bPendingModelResolve = true;

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    registry.emplace_or_replace<DirtyRenderTransformComponent>(entity);
}

void SplineMeshComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    registry.remove<MeshRuntime>(entity);
    registry.remove<SplineMeshLoadingTag>(entity);
    registry.remove<RenderTransformComponent>(entity);
    registry.remove<DirtyRenderTransformComponent>(entity);
}
} // Game::Component

namespace Game
{
bool Component::SplineMeshComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return !registry.any_of<Component::StaticMeshComponent, Component::ProceduralMeshComponent>(entity);
}

void Component::SplineMeshComponent::Serialize(const SplineMeshComponent& comp, nlohmann::json& json)
{
    json["radius"] = comp.radius;
    json["rollAngle"] = comp.rollAngle;
    json["sides"] = comp.sides;
    json["segmentsPerSpan"] = comp.segmentsPerSpan;
    json["bCaps"] = comp.bCaps;
    json["bDualPath"] = comp.bDualPath;
    json["dualPathSpacing"] = comp.dualPathSpacing;
    json["bCrossPlanks"] = comp.bCrossPlanks;
    json["crossPlankInterval"] = comp.crossPlankInterval;
    json["crossPlankHeight"] = comp.crossPlankHeight;
    json["material"] = comp.material.id;

    json["controlPoints"] = nlohmann::json::array();
    for (const auto& cp : comp.controlPoints) {
        json["controlPoints"].push_back({cp.x, cp.y, cp.z, cp.w});
    }
}

void Component::SplineMeshComponent::Deserialize(SplineMeshComponent& comp, const nlohmann::json& json)
{
    comp.radius = json["radius"].get<float>();
    comp.rollAngle = json.value("rollAngle", 0.0f);
    comp.sides = json["sides"].get<int32_t>();
    comp.segmentsPerSpan = json["segmentsPerSpan"].get<int32_t>();
    comp.bCaps = json.value("bCaps", true);
    comp.bDualPath = json.value("bDualPath", false);
    comp.dualPathSpacing = json.value("dualPathSpacing", 1.0f);
    comp.bCrossPlanks = json.value("bCrossPlanks", false);
    comp.crossPlankInterval = json.value("crossPlankInterval", 4);
    comp.crossPlankHeight = json.value("crossPlankHeight", 0.0f);
    comp.material = Engine::MaterialID(json["material"].get<uint64_t>());

    comp.controlPoints.Clear();
    for (const auto& cpJson : json["controlPoints"]) {
        float w = cpJson.size() > 3 ? cpJson[3].get<float>() : 0.0f;
        comp.controlPoints.PushBack({cpJson[0].get<float>(), cpJson[1].get<float>(), cpJson[2].get<float>(), w});
    }
    if (comp.controlPoints.Size() < 2) {
        comp.controlPoints.Clear();
        comp.controlPoints.PushBack({0,0,0,0});
        comp.controlPoints.PushBack({0,0,1,0});
        comp.controlPoints.PushBack({0,0,2,0});
        comp.controlPoints.PushBack({0,0,3,0});
    }
}

ComponentEditorResult Component::SplineMeshComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity,
                                                                          const char* name)
{
    auto& component = registry.get<SplineMeshComponent>(entity);
    static int editPointIdx = -1;
    static entt::entity editEntity = entt::null;
    static bool wasUsingGizmo = false;

    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto* state = registry.ctx().get<Engine::GameState*>();

    if (editEntity != entity) {
        editPointIdx = -1;
        editEntity = entity;
        wasUsingGizmo = false;
    }

    bool hasGizmoClaim = editPointIdx != -1 && !state->bCustomGizmoActive;


    bool open = ImGui::CollapsingHeader("Spline Mesh", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletesplinemesh");
    ImGui::PopStyleColor();

    if (open) {
        bool visible = component.modelFlags.x != 0.0f;
        bool shadowCaster = component.modelFlags.y != 0.0f;
        if (ImGui::Checkbox("Visible##splinemesh", &visible)) component.modelFlags.x = visible ? 1.0f : 0.0f;
        ImGui::SameLine();
        if (ImGui::Checkbox("Shadow Caster##splinemesh", &shadowCaster)) component.modelFlags.y = shadowCaster ? 1.0f : 0.0f;

        bool dirty = false;

        ImGui::DragFloat("Radius", &component.radius, 0.01f, 0.001f, 50.0f);
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Roll Angle", &component.rollAngle, 1.0f, -180.0f, 180.0f, "%.1f deg");
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragInt("Sides", &component.sides, 1, 3, 64);
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragInt("Segments/Span", &component.segmentsPerSpan, 1, 1, 32);
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Checkbox("Caps", &component.bCaps)) { dirty = true; }

        if (ImGui::Checkbox("Dual Path", &component.bDualPath)) { dirty = true; }
        if (component.bDualPath) {
            ImGui::DragFloat("Path Spacing", &component.dualPathSpacing, 0.01f, 0.01f, 50.0f);
            dirty |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::Checkbox("Cross Planks", &component.bCrossPlanks)) { dirty = true; }
            if (component.bCrossPlanks) {
                ImGui::DragInt("Plank Interval", &component.crossPlankInterval, 1, 1, 32);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DragFloat("Plank Height", &component.crossPlankHeight, 0.01f, -10.0f, 10.0f);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
            }
        }

        ImGui::SeparatorText("Control Points");

        int pointToRemove = -1;
        for (int i = 0; i < static_cast<int>(component.controlPoints.Size()); i++) {
            ImGui::PushID(i);
            const bool isEditing = (editPointIdx == i);

            ImGui::PushStyleColor(ImGuiCol_Button, isEditing ? ImVec4(0.15f, 0.65f, 0.15f, 1.0f) : ImVec4(0.15f, 0.35f, 0.65f, 1.0f));
            ImGui::BeginDisabled((state->bCustomGizmoActive || state->bCustomGizmoActivePrev) && !isEditing);
            if (ImGui::SmallButton(isEditing ? "D##edit" : "E##edit")) {
                editPointIdx = isEditing ? -1 : i;
                if (editPointIdx == -1) { hasGizmoClaim = false; }
            }
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            ImGui::SameLine();

            char label[16];
            snprintf(label, sizeof(label), "##cp%d", i);
            if (ImGui::DragFloat3(label, &component.controlPoints[i].x, 0.01f)) {}
            dirty |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();

            ImGui::BeginDisabled(static_cast<int>(component.controlPoints.Size()) <= 2);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::SmallButton("X##rmpt")) {
                pointToRemove = i;
                if (editPointIdx == i) { editPointIdx = -1; }
                else if (editPointIdx > i) { editPointIdx--; }
            }
            ImGui::PopStyleColor();
            ImGui::EndDisabled();

            {
                char rollLabel[16];
                snprintf(rollLabel, sizeof(rollLabel), "##roll%d", i);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat(rollLabel, &component.controlPoints[i].w, 1.0f, -360.0f, 360.0f, "%.1f\xC2\xB0");
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
            }

            ImGui::PopID();
        }

        if (pointToRemove >= 0) {
            component.controlPoints.RemoveAt(static_cast<size_t>(pointToRemove));
            dirty = true;
        }

        ImGui::BeginDisabled(component.controlPoints.IsFull());
        if (ImGui::Button("Add Point")) {
            const glm::vec3 last = glm::vec3(component.controlPoints.Back());
            const glm::vec3 prev = component.controlPoints.Size() >= 2
                                       ? glm::vec3(component.controlPoints[component.controlPoints.Size() - 2])
                                       : last - glm::vec3(0, 0, 1);
            component.controlPoints.PushBack(glm::vec4(last + glm::normalize(last - prev), 0.0f));
            dirty = true;
        }
        ImGui::EndDisabled();

        if (editPointIdx == -1) { hasGizmoClaim = false; }
        if (hasGizmoClaim && editPointIdx < static_cast<int>(component.controlPoints.Size())) {
            auto* transform = registry.try_get<TransformComponent>(entity);
            if (transform) {
                const glm::mat4 view = viewFamily.mainView.currentViewData.view;
                const glm::mat4 proj = viewFamily.mainView.currentViewData.proj;
                const glm::mat4 entityMat = glm::translate(glm::mat4(1.0f), transform->translation) * glm::mat4_cast(transform->rotation);
                const glm::mat4 entityMatInv = glm::inverse(entityMat);
                const int idx = editPointIdx;
                const int cpCount = static_cast<int>(component.controlPoints.Size());

                auto cpPos = glm::vec3(component.controlPoints[idx]);
                glm::vec3 worldPt = glm::vec3(entityMat * glm::vec4(cpPos, 1.0f));

                glm::vec3 localTangent;
                if (cpCount < 2) localTangent = glm::vec3(0, 0, 1);
                else if (idx == 0) localTangent = glm::normalize(glm::vec3(component.controlPoints[1]) - glm::vec3(component.controlPoints[0]));
                else if (idx == cpCount - 1) localTangent = glm::normalize(glm::vec3(component.controlPoints[cpCount - 1]) - glm::vec3(component.controlPoints[cpCount - 2]));
                else localTangent = glm::normalize(glm::vec3(component.controlPoints[idx + 1]) - glm::vec3(component.controlPoints[idx - 1]));

                glm::vec3 worldTangent = glm::normalize(glm::vec3(entityMat * glm::vec4(localTangent, 0.0f)));

                glm::vec3 refUp = {0, 1, 0};
                if (glm::abs(glm::dot(worldTangent, refUp)) > 0.99f) refUp = {1, 0, 0};
                glm::vec3 baseRight = glm::normalize(glm::cross(refUp, worldTangent));
                glm::vec3 baseUp = glm::normalize(glm::cross(worldTangent, baseRight));

                float currentRoll = glm::radians(component.controlPoints[idx].w);
                glm::vec3 rRight = glm::cos(currentRoll) * baseRight + glm::sin(currentRoll) * baseUp;
                glm::vec3 rUp = -glm::sin(currentRoll) * baseRight + glm::cos(currentRoll) * baseUp;

                glm::mat4 mat(1.0f);
                mat[0] = glm::vec4(rRight, 0);
                mat[1] = glm::vec4(rUp, 0);
                mat[2] = glm::vec4(worldTangent, 0);
                mat[3] = glm::vec4(worldPt, 1);

                const auto gizmoOp = (state->currentGizmoOperation == ImGuizmo::SCALE)
                    ? ImGuizmo::TRANSLATE
                    : state->currentGizmoOperation;

                ImGuizmo::PushID(editPointIdx);
                if (ImGuizmo::Manipulate(
                    glm::value_ptr(view), glm::value_ptr(proj),
                    gizmoOp, ImGuizmo::LOCAL,
                    glm::value_ptr(mat))) {
                    glm::vec3 newPos = glm::vec3(entityMatInv * glm::vec4(glm::vec3(mat[3]), 1.0f));
                    glm::vec3 newRight = glm::normalize(glm::vec3(mat[0]));
                    float newRoll = glm::degrees(glm::atan(glm::dot(newRight, baseUp), glm::dot(newRight, baseRight)));
                    component.controlPoints[idx] = glm::vec4(newPos, newRoll);
                }
                const bool usingGizmo = ImGuizmo::IsUsing();
                ImGuizmo::PopID();
                if (!usingGizmo && wasUsingGizmo) {
                    dirty = true;
                }
                wasUsingGizmo = usingGizmo;
            }
        }

        // Debug: draw control polygon and point spheres
        {
            constexpr glm::vec4 kLineColor{0.3f, 0.7f, 1.0f, 1.0f};
            constexpr glm::vec4 kPointColor{0.5f, 0.9f, 1.0f, 1.0f};
            constexpr glm::vec4 kEditColor{1.0f, 0.7f, 0.1f, 1.0f};
            constexpr float kPointRadius = 0.08f;
            auto* transform = registry.try_get<TransformComponent>(entity);
            const glm::mat4 entityMat = transform
                                            ? glm::translate(glm::mat4(1.0f), transform->translation) * glm::mat4_cast(transform->rotation)
                                            : glm::mat4(1.0f);

            for (int i = 0; i < static_cast<int>(component.controlPoints.Size()); i++) {
                glm::vec3 wp = glm::vec3(entityMat * glm::vec4(glm::vec3(component.controlPoints[i]), 1.0f));
                DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {wp, kPointRadius, (i == editPointIdx) ? kEditColor : kPointColor});
                if (i + 1 < static_cast<int>(component.controlPoints.Size())) {
                    glm::vec3 wp2 = glm::vec3(entityMat * glm::vec4(glm::vec3(component.controlPoints[i + 1]), 1.0f));
                    DEBUG_ADD_LINE(viewFamily.debugLines, {wp, wp2, kLineColor});
                }
            }
        }

        // Material selector
        {
            const char* currentLabel = "(none)";
            if (component.material.IsValid()) {
                if (const Engine::Material* m = ctx->materialManager->GetMaterial(component.material)) {
                    currentLabel = m->name.c_str();
                }
            }
            auto* runtime = registry.try_get<MeshRuntime>(entity);
            if (ImGui::BeginCombo("Material##spline", currentLabel)) {
                if (ImGui::Selectable("(none)", !component.material.IsValid())) {
                    if (component.material.IsValid()) {
                        if (runtime && !runtime->primitives.IsEmpty()) {
                            ctx->materialManager->ReleaseMaterial(runtime->primitives[0].materialID);
                            runtime->primitives.Clear();
                        }
                        component.material = Engine::MaterialID{};
                        registry.emplace_or_replace<SplineMeshLoadingTag>(entity);
                        state->bPendingModelResolve = true;
                    }
                }
                for (const auto& [matId, mat] : ctx->materialManager->GetMaterials()) {
                    if (mat.immutable) continue;
                    if (ImGui::Selectable(mat.name.c_str(), matId == component.material)) {
                        if (matId != component.material) {
                            if (runtime && !runtime->primitives.IsEmpty()) {
                                ctx->materialManager->ReleaseMaterial(runtime->primitives[0].materialID);
                                runtime->primitives.Clear();
                            }
                            component.material = matId;
                            registry.emplace_or_replace<SplineMeshLoadingTag>(entity);
                            state->bPendingModelResolve = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (dirty) {
            auto& runtime = registry.get_or_emplace<MeshRuntime>(entity);
            if (runtime.modelHandle.IsValid()) {
                ctx->assetManager->UnloadModel(runtime.modelHandle);
                runtime.modelHandle = {};
            }
            for (size_t i = 0; i < runtime.primitives.Size(); ++i) {
                ctx->materialManager->ReleaseMaterial(runtime.primitives[i].materialID);
            }
            runtime.primitives.Clear();
            runtime.modelHandle = ctx->assetManager->LoadSplineModel(ToSplineParams(component));
            registry.emplace_or_replace<SplineMeshLoadingTag>(entity);
            state->bPendingModelResolve = true;
        }
    }

    if (hasGizmoClaim) { state->bCustomGizmoActive = true; }

    return {.requestRemoval = remove};
}
} // Game
