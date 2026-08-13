//
// Created by William on 2026-03-21.
//

#include "spline_mesh_component.h"

#include <algorithm>
#include <glm/gtc/type_ptr.hpp>
#include <json/nlohmann/json.hpp>

#include "imgui.h"
#include "ImGuizmo.h"

#include "static_mesh_component.h"
#include "text3d_component.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/spline/spline.h"
#include "game/component-registry/editor_gizmo_helpers.h"
#include "game/components/component_types.h"
#include "game/components/core_components.h"

namespace Game::Component
{
Engine::SplineParams ToSplineParams(const SplineMeshComponent& component)
{
    Engine::SplineParams params{};
    params.spline = component.spline;
    params.radius = component.radius;
    params.rollAngle = component.rollAngle;
    params.sides = component.sides;
    params.segmentsPerSpan = component.segmentsPerSpan;
    params.bCaps = component.bCaps;
    params.bCrossPlanks = component.bCrossPlanks;
    params.crossPlankInterval = component.crossPlankInterval;
    params.crossPlankHeight = component.crossPlankHeight;
    params.crossPlankThickness = component.crossPlankThickness;
    params.crossPlankLength = component.crossPlankLength;
    params.profile = component.profile;
    params.railing = component.railing;
    return params;
}

void SplineMeshComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    registry.get_or_emplace<RenderFlagsComponent>(entity);
    auto& component = registry.get<SplineMeshComponent>(entity);
    auto* state = registry.ctx().get<Engine::EngineState*>();

    if (component.spline.points.IsEmpty()) {
        component.spline.points.PushBack({0, 0, 0});
        component.spline.points.PushBack({0, 0, 1});
        component.spline.points.PushBack({0, 0, 2});
        component.spline.points.PushBack({0, 0, 3});
        component.spline.rolls.PushBack(0.0f);
        component.spline.rolls.PushBack(0.0f);
        component.spline.rolls.PushBack(0.0f);
        component.spline.rolls.PushBack(0.0f);
    }

    registry.emplace_or_replace<SplineMeshLoadPendingTag>(entity);
    state->assetLoad.bPendingModelResolve = true;

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
}

void SplineMeshComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    registry.remove<MeshRuntime>(entity);
    registry.remove<SplineMeshLoadPendingTag>(entity);
    registry.remove<SplineMeshLoadingTag>(entity);
    registry.remove<RenderTransformComponent>(entity);
}
} // Game::Component

namespace Game
{
bool Component::SplineMeshComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return !registry.any_of<Component::StaticMeshComponent, Component::ProceduralMeshComponent, Component::Text3DComponent>(entity);
}

void Component::SplineMeshComponent::Serialize(const SplineMeshComponent& comp, nlohmann::json& json)
{
    Engine::Spline::Serialize(comp.spline, json["spline"]);
    json["radius"] = comp.radius;
    json["rollAngle"] = comp.rollAngle;
    json["sides"] = comp.sides;
    json["segmentsPerSpan"] = comp.segmentsPerSpan;
    json["bCaps"] = comp.bCaps;
    json["bCrossPlanks"] = comp.bCrossPlanks;
    json["crossPlankInterval"] = comp.crossPlankInterval;
    json["crossPlankHeight"] = comp.crossPlankHeight;
    json["crossPlankThickness"] = comp.crossPlankThickness;
    json["crossPlankLength"] = comp.crossPlankLength;
    json["profileType"] = static_cast<int32_t>(comp.profile.type);
    json["profileWidth"] = comp.profile.width;
    json["profileHeight"] = comp.profile.height;
    json["profileCornerRadius"] = comp.profile.cornerRadius;
    json["profileCornerSegments"] = comp.profile.cornerSegments;
    json["profileThickness"] = comp.profile.thickness;
    json["railingEnabled"] = comp.railing.bEnabled;
    json["railingPosts"] = comp.railing.bPosts;
    json["railingPostInterval"] = comp.railing.postInterval;
    json["railingPostBottom"] = comp.railing.postBottom;
    json["railingPostTop"] = comp.railing.postTop;
    json["railingPostSizeX"] = comp.railing.postSize.x;
    json["railingPostSizeY"] = comp.railing.postSize.y;
    json["railingPostLateral"] = comp.railing.postLateral;
    json["railingLateralOffset"] = comp.railing.lateralOffset;
    auto& lanesJson = json["railingLanes"] = nlohmann::json::array();
    for (int i = 0; i < static_cast<int>(comp.railing.lanes.Size()); i++) {
        lanesJson.push_back({comp.railing.lanes[i].x, comp.railing.lanes[i].y});
    }
    json["material"] = comp.material.id;
}

void Component::SplineMeshComponent::Deserialize(SplineMeshComponent& comp, const nlohmann::json& json)
{
    if (json.contains("spline")) {
        Engine::Spline::Deserialize(comp.spline, json["spline"]);
    }

    comp.radius = json.value("radius", 0.5f);
    comp.rollAngle = json.value("rollAngle", 0.0f);
    comp.sides = json.value("sides", 8);
    comp.segmentsPerSpan = json.value("segmentsPerSpan", 8);
    comp.bCaps = json.value("bCaps", true);
    comp.bCrossPlanks = json.value("bCrossPlanks", false);
    comp.crossPlankInterval = json.value("crossPlankInterval", 4);
    comp.crossPlankHeight = json.value("crossPlankHeight", 0.0f);
    comp.crossPlankThickness = json.value("crossPlankThickness", 0.1f);
    comp.crossPlankLength = json.value("crossPlankLength", 0.3f);
    comp.profile.type = static_cast<Engine::SplineProfileType>(json.value("profileType", 0));
    comp.profile.width = json.value("profileWidth", 0.4f);
    comp.profile.height = json.value("profileHeight", 0.4f);
    comp.profile.cornerRadius = json.value("profileCornerRadius", 0.08f);
    comp.profile.cornerSegments = json.value("profileCornerSegments", 3);
    comp.profile.thickness = json.value("profileThickness", 0.05f);
    comp.railing.bEnabled = json.value("railingEnabled", false);
    comp.railing.bPosts = json.value("railingPosts", true);
    comp.railing.postInterval = json.value("railingPostInterval", 4);
    comp.railing.postBottom = json.value("railingPostBottom", 0.0f);
    comp.railing.postTop = json.value("railingPostTop", 1.0f);
    comp.railing.postSize.x = json.value("railingPostSizeX", 0.05f);
    comp.railing.postSize.y = json.value("railingPostSizeY", 0.05f);
    comp.railing.postLateral = json.value("railingPostLateral", 0.0f);
    comp.railing.lateralOffset = json.value("railingLateralOffset", 0.0f);
    comp.railing.lanes.Clear();
    if (json.contains("railingLanes")) {
        for (const auto& e : json["railingLanes"]) {
            if (comp.railing.lanes.Size() >= 8) { break; }
            comp.railing.lanes.PushBack(Vec2{e[0].get<float>(), e[1].get<float>()});
        }
    }
    comp.material = Engine::MaterialID(json.value("material", uint64_t(0)));

    if (comp.spline.points.Size() < 2) {
        comp.spline.points.Clear();
        comp.spline.rolls.Clear();
        comp.spline.points.PushBack({0, 0, 0});
        comp.spline.points.PushBack({0, 0, 1});
        comp.spline.points.PushBack({0, 0, 2});
        comp.spline.points.PushBack({0, 0, 3});
        comp.spline.rolls.PushBack(0.0f);
        comp.spline.rolls.PushBack(0.0f);
        comp.spline.rolls.PushBack(0.0f);
        comp.spline.rolls.PushBack(0.0f);
    }
}

Engine::ComponentEditorResult Component::SplineMeshComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity,
                                                                          const char* name)
{
    auto& component = registry.get<SplineMeshComponent>(entity);
    static int editPointIdx = -1;
    static entt::entity editEntity = entt::null;
    static bool wasUsingGizmo = false;

    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();

    if (editEntity != entity) {
        editPointIdx = -1;
        editEntity = entity;
        wasUsingGizmo = false;
    }

    bool hasGizmoClaim = editPointIdx != -1;

    bool open = ImGui::CollapsingHeader("Spline Mesh", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletesplinemesh");
    ImGui::PopStyleColor();

    bool modified = false;
    if (open) {
        auto& renderFlags = registry.get_or_emplace<RenderFlagsComponent>(entity);
        bool visible = renderFlags.Has(RenderFlagsComponent::VISIBLE);
        if (ImGui::Checkbox("Visible##splinemesh", &visible)) { renderFlags.Set(RenderFlagsComponent::VISIBLE, visible); }
        bool probeBakeExclude = !renderFlags.Has(RenderFlagsComponent::PROBE_BAKE_INCLUDE);
        if (ImGui::Checkbox("Probe Bake Exclude##splinemesh", &probeBakeExclude)) { renderFlags.Set(RenderFlagsComponent::PROBE_BAKE_INCLUDE, !probeBakeExclude); }

        bool dirty = false;

        int splineModeInt = static_cast<int>(component.spline.mode);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("Spline Mode##sm", &splineModeInt, Engine::SplineModeNames, static_cast<int>(Engine::SplineMode::COUNT))) {
            component.spline.mode = static_cast<Engine::SplineMode>(splineModeInt);
            dirty = true;
        }

        const char* splineProfileNames[] = {"Tube", "Rectangle", "Rounded Rect", "I-Beam", "U-Channel", "L-Angle", "Rail Head", "Handrail"};
        int profileTypeInt = static_cast<int>(component.profile.type);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("Profile##sm", &profileTypeInt, splineProfileNames, IM_ARRAYSIZE(splineProfileNames))) {
            component.profile.type = static_cast<Engine::SplineProfileType>(profileTypeInt);
            dirty = true;
        }

        const Engine::SplineProfileType pt = component.profile.type;
        if (pt == Engine::SplineProfileType::Tube) {
            ImGui::DragFloat("Radius", &component.radius, 0.01f, 0.001f, 50.0f);
            dirty |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragInt("Sides", &component.sides, 1, 3, 64);
            dirty |= ImGui::IsItemDeactivatedAfterEdit();
        }
        else {
            ImGui::DragFloat("Width", &component.profile.width, 0.01f, 0.001f, 50.0f);
            dirty |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Height", &component.profile.height, 0.01f, 0.001f, 50.0f);
            dirty |= ImGui::IsItemDeactivatedAfterEdit();
            if (pt == Engine::SplineProfileType::RoundedRect) {
                ImGui::DragFloat("Corner Radius", &component.profile.cornerRadius, 0.005f, 0.0f, 25.0f);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
            }
            if (pt == Engine::SplineProfileType::RoundedRect || pt == Engine::SplineProfileType::Handrail) {
                ImGui::DragInt("Corner Segments", &component.profile.cornerSegments, 1, 1, 16);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
            }
            if (pt == Engine::SplineProfileType::IBeam || pt == Engine::SplineProfileType::UChannel
                || pt == Engine::SplineProfileType::LAngle || pt == Engine::SplineProfileType::RailHead) {
                ImGui::DragFloat("Thickness", &component.profile.thickness, 0.005f, 0.001f, 25.0f);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
            }
        }

        ImGui::DragFloat("Roll Angle", &component.rollAngle, 1.0f, -180.0f, 180.0f, "%.1f deg");
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragInt("Segments/Span", &component.segmentsPerSpan, 1, 1, 32);
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::BeginDisabled(component.spline.bClosed);
        if (ImGui::Checkbox("Caps", &component.bCaps)) { dirty = true; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Checkbox("Closed", &component.spline.bClosed)) { dirty = true; }

        if (ImGui::Checkbox("Railing", &component.railing.bEnabled)) { dirty = true; }
        if (component.railing.bEnabled) {
            auto& rail = component.railing;
            ImGui::DragFloat("Lateral Offset", &rail.lateralOffset, 0.01f, -50.0f, 50.0f);
            dirty |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SeparatorText("Lanes (lateral, vertical)");
            if (ImGui::SmallButton("2-Rail")) {
                rail.lanes.Clear();
                rail.lanes.PushBack(Vec2{0.0f, 1.0f});
                rail.lanes.PushBack(Vec2{0.0f, 0.5f});
                dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("3-Rail")) {
                rail.lanes.Clear();
                rail.lanes.PushBack(Vec2{0.0f, 1.0f});
                rail.lanes.PushBack(Vec2{0.0f, 0.66f});
                rail.lanes.PushBack(Vec2{0.0f, 0.33f});
                dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Dual")) {
                rail.lanes.Clear();
                rail.lanes.PushBack(Vec2{-0.5f, 0.0f});
                rail.lanes.PushBack(Vec2{0.5f, 0.0f});
                dirty = true;
            }

            int laneToRemove = -1;
            for (int li = 0; li < static_cast<int>(rail.lanes.Size()); li++) {
                ImGui::PushID(2000 + li);
                float lane2[2] = {rail.lanes[li].x, rail.lanes[li].y};
                if (ImGui::DragFloat2("##lane", lane2, 0.01f, -50.0f, 50.0f)) {
                    rail.lanes[li] = Vec2{lane2[0], lane2[1]};
                }
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) { laneToRemove = li; }
                ImGui::PopID();
            }
            if (laneToRemove >= 0) {
                rail.lanes.RemoveAt(static_cast<size_t>(laneToRemove));
                dirty = true;
            }
            if (rail.lanes.Size() < 8 && ImGui::SmallButton("Add Lane")) {
                rail.lanes.PushBack(Vec2{0.0f, 0.0f});
                dirty = true;
            }

            ImGui::BeginDisabled(rail.lanes.Size() != 2);
            if (ImGui::Checkbox("Cross Planks", &component.bCrossPlanks)) { dirty = true; }
            if (component.bCrossPlanks && rail.lanes.Size() == 2) {
                ImGui::DragInt("Plank Interval", &component.crossPlankInterval, 1, 1, 32);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DragFloat("Plank Height", &component.crossPlankHeight, 0.01f, -10.0f, 10.0f);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DragFloat("Plank Thickness", &component.crossPlankThickness, 0.005f, 0.001f, 10.0f);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DragFloat("Plank Length", &component.crossPlankLength, 0.005f, 0.001f, 50.0f);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
            }
            ImGui::EndDisabled();

            if (ImGui::Checkbox("Posts", &rail.bPosts)) { dirty = true; }
            if (rail.bPosts) {
                ImGui::DragInt("Post Interval", &rail.postInterval, 1, 1, 64);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DragFloat("Post Bottom", &rail.postBottom, 0.01f, -10.0f, 10.0f);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DragFloat("Post Top", &rail.postTop, 0.01f, -10.0f, 10.0f);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
                // Cap post cross-section at the rail width so the post never pokes out from under the rail it meets.
                const float railWidth = (component.profile.type == Engine::SplineProfileType::Tube)
                                            ? 2.0f * component.radius
                                            : std::max(component.profile.width, component.profile.height);
                float postSize[2] = {std::min(rail.postSize.x, railWidth), std::min(rail.postSize.y, railWidth)};
                if (ImGui::DragFloat2("Post Size", postSize, 0.005f, 0.001f, railWidth, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
                    rail.postSize = Vec2{postSize[0], postSize[1]};
                }
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DragFloat("Post Lateral", &rail.postLateral, 0.01f, -50.0f, 50.0f);
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
            }
        }

        ImGui::SeparatorText("Control Points");

        int pointToRemove = -1;
        int pointToSwap = -1;
        const int cpCount = static_cast<int>(component.spline.points.Size());
        for (int i = 0; i < cpCount; i++) {
            ImGui::PushID(i);
            const bool isEditing = (editPointIdx == i);

            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { pointToSwap = i - 1; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i == cpCount - 1);
            if (ImGui::ArrowButton("##dn", ImGuiDir_Down)) { pointToSwap = i; }
            ImGui::EndDisabled();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, isEditing ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
            ImGui::BeginDisabled((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !isEditing);
            if (ImGui::SmallButton(isEditing ? "D##edit" : "E##edit")) {
                editPointIdx = isEditing ? -1 : i;
                if (editPointIdx == -1) { hasGizmoClaim = false; }
            }
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            ImGui::SameLine();

            char label[16];
            snprintf(label, sizeof(label), "##cp%d", i);
            if (ImGui::DragFloat3(label, &component.spline.points[i].x, 0.01f)) {}
            dirty |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();

            ImGui::BeginDisabled(cpCount <= 2);
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
                float roll = (i < static_cast<int>(component.spline.rolls.Size())) ? component.spline.rolls[i] : 0.0f;
                if (ImGui::DragFloat(rollLabel, &roll, 1.0f, -360.0f, 360.0f, "%.1f\xC2\xB0")) {
                    if (i < static_cast<int>(component.spline.rolls.Size())) { component.spline.rolls[i] = roll; }
                }
                dirty |= ImGui::IsItemDeactivatedAfterEdit();
            }

            ImGui::PopID();
        }

        if (pointToRemove >= 0) {
            component.spline.points.RemoveAt(static_cast<size_t>(pointToRemove));
            if (pointToRemove < static_cast<int>(component.spline.rolls.Size())) {
                component.spline.rolls.RemoveAt(static_cast<size_t>(pointToRemove));
            }
            dirty = true;
        }
        if (pointToSwap >= 0 && pointToSwap + 1 < cpCount) {
            std::swap(component.spline.points[pointToSwap], component.spline.points[pointToSwap + 1]);
            if (pointToSwap < static_cast<int>(component.spline.rolls.Size()) - 1) {
                std::swap(component.spline.rolls[pointToSwap], component.spline.rolls[pointToSwap + 1]);
            }
            if (editPointIdx == pointToSwap) { editPointIdx = pointToSwap + 1; }
            else if (editPointIdx == pointToSwap + 1) { editPointIdx = pointToSwap; }
            dirty = true;
        }

        ImGui::BeginDisabled(component.spline.points.IsFull());
        if (ImGui::Button("Add Point")) {
            const glm::vec3 last = component.spline.points.Back();
            const glm::vec3 prev = component.spline.points.Size() >= 2
                                       ? component.spline.points[component.spline.points.Size() - 2]
                                       : last - glm::vec3(0, 0, 1);
            component.spline.points.PushBack(last + glm::normalize(last - prev));
            component.spline.rolls.PushBack(component.spline.rolls.IsEmpty() ? 0.0f : component.spline.rolls.Back());
            dirty = true;
        }
        ImGui::EndDisabled();


        const int liveCpCount = static_cast<int>(component.spline.points.Size());

        auto* transform = registry.try_get<TransformComponent>(entity);
        glm::mat4 entityMat(1.0f);
        if (transform) {
            const auto* world = registry.try_get<WorldTransformComponent>(entity);
            const auto* rt = registry.try_get<RenderTransformComponent>(entity);
            const glm::vec3 renderOffset = rt ? rt->renderOffset : glm::vec3(0.0f);
            const glm::quat renderRotation = rt ? rt->renderRotation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            entityMat = glm::translate(world ? GetMatrix(*world) : glm::mat4(1.0f), renderOffset) * glm::mat4_cast(renderRotation);
        }
        const glm::mat4 entityMatInv = glm::inverse(entityMat);

        if (editPointIdx == -1) { hasGizmoClaim = false; }
        if (hasGizmoClaim && transform && editPointIdx < liveCpCount) {
            const glm::mat4 view = viewFamily.mainView.currentViewData.view;
            const glm::mat4 proj = viewFamily.mainView.currentViewData.proj;
            const int idx = editPointIdx;

            const glm::vec3& cpPos = component.spline.points[idx];
            glm::vec3 worldPt = glm::vec3(entityMat * glm::vec4(cpPos, 1.0f));

            glm::vec3 localTangent;
            if (liveCpCount < 2) { localTangent = glm::vec3(0, 0, 1); }
            else if (idx == 0) { localTangent = glm::normalize(component.spline.points[1] - component.spline.points[0]); }
            else if (idx == liveCpCount - 1) { localTangent = glm::normalize(component.spline.points[liveCpCount - 1] - component.spline.points[liveCpCount - 2]); }
            else { localTangent = glm::normalize(component.spline.points[idx + 1] - component.spline.points[idx - 1]); }

            glm::vec3 worldTangent = glm::normalize(glm::vec3(entityMat * glm::vec4(localTangent, 0.0f)));

            glm::vec3 refUp = {0, 1, 0};
            if (glm::abs(glm::dot(worldTangent, refUp)) > 0.99f) { refUp = {1, 0, 0}; }
            glm::vec3 baseRight = glm::normalize(glm::cross(refUp, worldTangent));
            glm::vec3 baseUp = glm::normalize(glm::cross(worldTangent, baseRight));

            float currentRoll = (idx < static_cast<int>(component.spline.rolls.Size())) ? glm::radians(component.spline.rolls[idx]) : 0.0f;
            glm::vec3 rRight = glm::cos(currentRoll) * baseRight + glm::sin(currentRoll) * baseUp;
            glm::vec3 rUp = -glm::sin(currentRoll) * baseRight + glm::cos(currentRoll) * baseUp;

            glm::mat4 mat(1.0f);
            mat[0] = glm::vec4(rRight, 0);
            mat[1] = glm::vec4(rUp, 0);
            mat[2] = glm::vec4(worldTangent, 0);
            mat[3] = glm::vec4(worldPt, 1);

            const auto gizmoOp = (state->editor.currentGizmoOperation == ImGuizmo::SCALE)
                ? ImGuizmo::TRANSLATE
                : state->editor.currentGizmoOperation;

            ImGuizmo::PushID(Editor::GizmoId::SPLINE_POINT_BASE + editPointIdx);
            if (ImGuizmo::Manipulate(
                glm::value_ptr(view), glm::value_ptr(proj),
                gizmoOp, ImGuizmo::LOCAL,
                glm::value_ptr(mat))) {
                component.spline.points[idx] = glm::vec3(entityMatInv * glm::vec4(glm::vec3(mat[3]), 1.0f));
                if (gizmoOp == ImGuizmo::ROTATE && idx < static_cast<int>(component.spline.rolls.Size())) {
                    glm::vec3 newRight = glm::normalize(glm::vec3(mat[0]));
                    component.spline.rolls[idx] = glm::degrees(glm::atan(glm::dot(newRight, baseUp), glm::dot(newRight, baseRight)));
                }
            }
            const bool usingGizmo = ImGuizmo::IsUsing();
            ImGuizmo::PopID();
            if (!usingGizmo && wasUsingGizmo) {
                dirty = true;
            }
            wasUsingGizmo = usingGizmo;
        }

        // Debug: draw control polygon and point spheres
        {
            constexpr glm::vec4 kLineColor{0.3f, 0.7f, 1.0f, 1.0f};
            constexpr glm::vec4 kPointColor{0.5f, 0.9f, 1.0f, 1.0f};
            constexpr glm::vec4 kEditColor{1.0f, 0.7f, 0.1f, 1.0f};
            constexpr float kPointRadius = 0.08f;

            for (int i = 0; i < liveCpCount; i++) {
                glm::vec3 wp = glm::vec3(entityMat * glm::vec4(component.spline.points[i], 1.0f));
                DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {wp, kPointRadius, (i == editPointIdx) ? kEditColor : kPointColor});
                const int nextI = component.spline.bClosed ? (i + 1) % liveCpCount : i + 1;
                if (nextI < liveCpCount || component.spline.bClosed) {
                    glm::vec3 wp2 = glm::vec3(entityMat * glm::vec4(component.spline.points[nextI], 1.0f));
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
            if (ImGui::BeginCombo("Material##spline", currentLabel)) {
                if (ImGui::Selectable("(none)", !component.material.IsValid())) {
                    if (component.material.IsValid()) {
                        component.material = Engine::MaterialID{};
                        registry.emplace_or_replace<SplineMeshLoadingTag>(entity);
                        state->assetLoad.bPendingModelResolve = true;
                        modified = true;
                    }
                }
                for (const auto& [matId, mat] : ctx->materialManager->GetMaterials()) {
                    if (mat.immutable) { continue; }
                    if (ImGui::Selectable(mat.name.c_str(), matId == component.material)) {
                        if (matId != component.material) {
                            component.material = matId;
                            registry.emplace_or_replace<SplineMeshLoadingTag>(entity);
                            state->assetLoad.bPendingModelResolve = true;
                            modified = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (dirty) {
            modified = true;
            registry.remove<MeshRuntime>(entity);
            registry.remove<SplineMeshLoadingTag>(entity);
            registry.emplace_or_replace<SplineMeshLoadPendingTag>(entity);
            state->assetLoad.bPendingModelResolve = true;
        }
    }

    if (hasGizmoClaim) { state->editor.bExclusiveGizmoActive = true; }

    return {.bRequestRemoval = remove, .bModified = modified};
}
} // Game
