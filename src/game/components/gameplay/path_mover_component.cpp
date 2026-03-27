//
// Created by William on 2026-03-26.
//

#include "path_mover_component.h"

#include <cmath>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <json/nlohmann/json.hpp>

#include "imgui.h"
#include "ImGuizmo.h"

#include "core/include/render_interface.h"
#include "engine/engine_api.h"
#include "game/component-registry/component_editor.h"
#include "game/components/core_components.h"

namespace Game::Component
{
float ApplyEasing(EasingType type, float t)
{
    switch (type) {
        case EasingType::Linear:
            return t;
        case EasingType::EaseInQuad:
            return t * t;
        case EasingType::EaseOutQuad:
            return t * (2.0f - t);
        case EasingType::EaseInOutQuad:
            return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
        case EasingType::EaseInCubic:
            return t * t * t;
        case EasingType::EaseOutCubic:
        {
            float u = t - 1.0f;
            return u * u * u + 1.0f;
        }
        case EasingType::EaseInOutCubic:
            return t < 0.5f ? 4.0f * t * t * t : 1.0f + (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f);
        case EasingType::EaseInQuart:
            return t * t * t * t;
        case EasingType::EaseOutQuart:
        {
            float u = t - 1.0f;
            return 1.0f - u * u * u * u;
        }
        case EasingType::EaseInOutQuart:
            return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - 8.0f * (t - 1.0f) * (t - 1.0f) * (t - 1.0f) * (t - 1.0f);
        case EasingType::EaseInExpo:
            return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
        case EasingType::EaseOutExpo:
            return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
        case EasingType::EaseInOutExpo:
            if (t <= 0.0f) { return 0.0f; }
            if (t >= 1.0f) { return 1.0f; }
            return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;
        case EasingType::EaseInSine:
            return 1.0f - std::cos(t * glm::pi<float>() * 0.5f);
        case EasingType::EaseOutSine:
            return std::sin(t * glm::pi<float>() * 0.5f);
        case EasingType::EaseInOutSine:
            return 0.5f * (1.0f - std::cos(glm::pi<float>() * t));
        default:
            return t;
    }
}

static glm::vec3 CatmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void EvaluatePath(const Core::InlineVector<PathControlPoint, PathMoverComponent::MAX_CONTROL_POINTS>& points, int32_t source, int32_t target, float progress, bool bIsLoop, glm::vec3& outPos,
                  glm::quat& outRot)
{
    if (points.Size() < 2) {
        if (points.Size() == 1) {
            outPos = points[0].position;
            outRot = points[0].rotation;
        }
        return;
    }

    float easedT = ApplyEasing(points[target].easing, progress);
    auto count = static_cast<int32_t>(points.Size());

    int32_t i0, i3;
    if (bIsLoop) {
        i0 = (source - (target > source ? 1 : -1) + count) % count;
        i3 = (target + (target > source ? 1 : -1) + count) % count;
    }
    else {
        if (source < target) {
            i0 = std::max(source - 1, 0);
            i3 = std::min(target + 1, count - 1);
        }
        else {
            i0 = std::min(source + 1, count - 1);
            i3 = std::max(target - 1, 0);
        }
    }

    outPos = CatmullRom(points[i0].position, points[source].position, points[target].position, points[i3].position, easedT);
    outRot = glm::slerp(points[source].rotation, points[target].rotation, easedT);
}

void PathMoverComponent::Serialize(const PathMoverComponent& comp, nlohmann::json& json)
{
    json["loopMode"] = comp.loopMode;
    json["controlPoints"] = nlohmann::json::array();
    for (size_t i = 0; i < comp.controlPoints.Size(); i++) {
        const auto& cp = comp.controlPoints[i];
        nlohmann::json cpJson;
        cpJson["position"] = {cp.position.x, cp.position.y, cp.position.z};
        cpJson["rotation"] = {cp.rotation.x, cp.rotation.y, cp.rotation.z, cp.rotation.w};
        cpJson["easing"] = cp.easing;
        cpJson["speed"] = cp.speed;
        cpJson["waitTime"] = cp.waitTime;
        json["controlPoints"].push_back(cpJson);
    }

    json["currentSegment"] = comp.currentSegment;
    json["progress"] = comp.progress;
    json["direction"] = comp.direction;
    json["bIsWaiting"] = comp.bIsWaiting;
    json["waitTimer"] = comp.waitTimer;
}

void PathMoverComponent::Deserialize(PathMoverComponent& comp, const nlohmann::json& json)
{
    comp.loopMode = static_cast<PathLoopMode>(json.value("loopMode", 0));
    comp.controlPoints.Clear();
    if (json.contains("controlPoints")) {
        for (const auto& cpJson : json["controlPoints"]) {
            PathControlPoint cp{};
            if (cpJson.contains("position")) {
                const auto& p = cpJson["position"];
                cp.position = {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()};
            }
            if (cpJson.contains("rotation")) {
                const auto& r = cpJson["rotation"];
                cp.rotation = glm::quat(r[3].get<float>(), r[0].get<float>(), r[1].get<float>(), r[2].get<float>());
            }
            cp.easing = static_cast<EasingType>(cpJson.value("easing", 0));
            cp.speed = cpJson.value("speed", 1.0f);
            cp.waitTime = cpJson.value("waitTime", 0.0f);
            comp.controlPoints.PushBack(cp);
        }
    }

    if (comp.controlPoints.IsEmpty()) {
        comp.controlPoints.PushBack({glm::vec3(0.0f), glm::quat(1, 0, 0, 0), EasingType::Linear});
    }

    comp.currentSegment = json.value("currentSegment", 0);
    comp.progress = json.value("progress", 0.0f);
    comp.direction = json.value("direction", 1);
    comp.bIsWaiting = json.value("bIsWaiting", false);
    comp.waitTimer = json.value("waitTimer", 0.0f);
}

ComponentEditorResult PathMoverComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& component = registry.get<PathMoverComponent>(entity);
    auto* state = registry.ctx().get<Engine::GameState*>();

    static int editPointIdx = -1;
    static entt::entity editEntity = entt::null;
    static bool wasUsingGizmo = false;

    if (editEntity != entity) {
        editPointIdx = -1;
        editEntity = entity;
        wasUsingGizmo = false;
    }

    bool hasGizmoClaim = editPointIdx != -1 && !state->bCustomGizmoActive;

    bool open = ImGui::CollapsingHeader("Path Mover", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletepathmover");
    ImGui::PopStyleColor();

    if (open) {
        int loopModeInt = static_cast<int>(component.loopMode);
        if (ImGui::Combo("Loop Mode", &loopModeInt, PathLoopModeNames, static_cast<int>(PathLoopMode::COUNT))) {
            component.loopMode = static_cast<PathLoopMode>(loopModeInt);
        }

        ImGui::SeparatorText("Runtime State");
        int maxSeg = std::max(0, static_cast<int>(component.controlPoints.Size()) - 1);
        ImGui::SliderInt("Segment", &component.currentSegment, 0, maxSeg);
        ImGui::SliderFloat("Progress", &component.progress, 0.0f, 1.0f, "%.3f");
        static const char* dirNames[] = {"Forward", "Backward"};
        int dirIdx = (component.direction >= 0) ? 0 : 1;
        if (ImGui::Combo("Direction", &dirIdx, dirNames, 2)) {
            component.direction = (dirIdx == 0) ? 1 : -1;
        }
        ImGui::Checkbox("Waiting", &component.bIsWaiting);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Wait Timer", &component.waitTimer, 0.05f, 0.0f, 60.0f, "%.2fs");

        ImGui::SeparatorText("Control Points");

        int pointToRemove = -1;
        for (int i = 0; i < static_cast<int>(component.controlPoints.Size()); i++) {
            ImGui::PushID(i);
            auto& cp = component.controlPoints[i];
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

            char posLabel[16];
            snprintf(posLabel, sizeof(posLabel), "##pos%d", i);
            ImGui::DragFloat3(posLabel, &cp.position.x, 0.01f);
            ImGui::SameLine();

            ImGui::BeginDisabled(static_cast<int>(component.controlPoints.Size()) <= 1);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::SmallButton("X##rmpt")) {
                pointToRemove = i;
                if (editPointIdx == i) { editPointIdx = -1; }
                else if (editPointIdx > i) { editPointIdx--; }
            }
            ImGui::PopStyleColor();
            ImGui::EndDisabled(); {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);
                char easingLabel[24];
                snprintf(easingLabel, sizeof(easingLabel), "##easing%d", i);
                ImGui::SetNextItemWidth(140.0f);
                int easingInt = static_cast<int>(cp.easing);
                if (ImGui::Combo(easingLabel, &easingInt, EasingTypeNames, static_cast<int>(EasingType::COUNT))) {
                    cp.easing = static_cast<EasingType>(easingInt);
                }
                ImGui::SameLine();
                char speedLabel[24];
                snprintf(speedLabel, sizeof(speedLabel), "##speed%d", i);
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat(speedLabel, &cp.speed, 0.01f, 0.001f, 100.0f, "spd %.2f");
                ImGui::SameLine();
                char waitLabel[24];
                snprintf(waitLabel, sizeof(waitLabel), "##wait%d", i);
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat(waitLabel, &cp.waitTime, 0.05f, 0.0f, 60.0f, "wait %.1fs");
            }

            ImGui::PopID();
        }

        if (pointToRemove >= 0) {
            component.controlPoints.RemoveAt(static_cast<size_t>(pointToRemove));
        }

        ImGui::BeginDisabled(component.controlPoints.IsFull());
        if (ImGui::Button("Add Point")) {
            PathControlPoint newPt{};
            if (component.controlPoints.Size() >= 2) {
                const auto& last = component.controlPoints.Back();
                const auto& prev = component.controlPoints[component.controlPoints.Size() - 2];
                newPt.position = last.position + glm::normalize(last.position - prev.position);
                newPt.rotation = last.rotation;
                newPt.speed = last.speed;
            }
            else if (component.controlPoints.Size() == 1) {
                newPt.position = component.controlPoints.Back().position + glm::vec3(0, 0, 1);
                newPt.rotation = component.controlPoints.Back().rotation;
                newPt.speed = component.controlPoints.Back().speed;
            }
            component.controlPoints.PushBack(newPt);
        }
        ImGui::EndDisabled();

        // Gizmo for editing individual control points
        if (editPointIdx == -1) { hasGizmoClaim = false; }
        if (hasGizmoClaim && editPointIdx < static_cast<int>(component.controlPoints.Size())) {
            auto* transform = registry.try_get<TransformComponent>(entity);
            if (transform) {
                const glm::mat4 view = viewFamily.mainView.currentViewData.view;
                const glm::mat4 proj = viewFamily.mainView.currentViewData.proj;
                const glm::mat4 entityMat = glm::translate(glm::mat4(1.0f), transform->translation) * glm::mat4_cast(transform->rotation);
                const glm::mat4 entityMatInv = glm::inverse(entityMat);
                const int idx = editPointIdx;

                auto& cp = component.controlPoints[idx];
                glm::vec3 worldPt = glm::vec3(entityMat * glm::vec4(cp.position, 1.0f));
                glm::quat worldRot = transform->rotation * cp.rotation;

                glm::mat4 mat = glm::translate(glm::mat4(1.0f), worldPt) * glm::mat4_cast(worldRot);

                const auto gizmoOp = (state->currentGizmoOperation == ImGuizmo::SCALE)
                                         ? ImGuizmo::TRANSLATE
                                         : state->currentGizmoOperation;

                ImGuizmo::PushID(editPointIdx);
                if (ImGuizmo::Manipulate(
                    glm::value_ptr(view), glm::value_ptr(proj),
                    gizmoOp, ImGuizmo::LOCAL,
                    glm::value_ptr(mat))) {
                    glm::vec3 newWorldPos = glm::vec3(mat[3]);
                    cp.position = glm::vec3(entityMatInv * glm::vec4(newWorldPos, 1.0f));

                    if (gizmoOp == ImGuizmo::ROTATE) {
                        glm::mat3 rotMat(mat);
                        rotMat[0] = glm::normalize(rotMat[0]);
                        rotMat[1] = glm::normalize(rotMat[1]);
                        rotMat[2] = glm::normalize(rotMat[2]);
                        glm::quat newWorldRot = glm::quat_cast(rotMat);
                        cp.rotation = glm::inverse(transform->rotation) * newWorldRot;
                    }
                }
                const bool usingGizmo = ImGuizmo::IsUsing();
                ImGuizmo::PopID();
                wasUsingGizmo = usingGizmo;
            }
        }

        // Debug draw: control point spheres, connecting lines, and spline curve
        {
            constexpr glm::vec4 kLineColor{0.2f, 1.0f, 0.3f, 1.0f};
            constexpr glm::vec4 kCurveColor{1.0f, 0.8f, 0.2f, 1.0f};
            constexpr glm::vec4 kPointColor{0.3f, 1.0f, 0.5f, 1.0f};
            constexpr glm::vec4 kEditColor{1.0f, 0.7f, 0.1f, 1.0f};
            constexpr glm::vec4 kProgressColor{1.0f, 0.2f, 0.2f, 1.0f};
            constexpr float kPointRadius = 0.1f;
            constexpr int kCurveSubdivisions = 32;

            auto* transform = registry.try_get<TransformComponent>(entity);
            const glm::mat4 entityMat = transform
                                            ? glm::translate(glm::mat4(1.0f), transform->translation) * glm::mat4_cast(transform->rotation)
                                            : glm::mat4(1.0f);

            for (int i = 0; i < static_cast<int>(component.controlPoints.Size()); i++) {
                glm::vec3 wp = glm::vec3(entityMat * glm::vec4(component.controlPoints[i].position, 1.0f));
                DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {wp, kPointRadius, (i == editPointIdx) ? kEditColor : kPointColor});
            }

            // Draw the actual spline curve
            if (component.controlPoints.Size() >= 2) {
                const bool bIsLoop = component.loopMode == PathLoopMode::Loop;
                const int count = static_cast<int>(component.controlPoints.Size());
                const int numSegments = bIsLoop ? count : count - 1;

                for (int seg = 0; seg < numSegments; seg++) {
                    const int source = seg;
                    const int target = (seg + 1) % count;

                    glm::vec3 prevPos;
                    glm::quat prevRot;
                    EvaluatePath(component.controlPoints, source, target, 0.0f, bIsLoop, prevPos, prevRot);
                    prevPos = glm::vec3(entityMat * glm::vec4(prevPos, 1.0f));

                    for (int step = 1; step <= kCurveSubdivisions; step++) {
                        float t = static_cast<float>(step) / static_cast<float>(kCurveSubdivisions);
                        glm::vec3 curPos;
                        glm::quat curRot;
                        EvaluatePath(component.controlPoints, source, target, t, bIsLoop, curPos, curRot);
                        curPos = glm::vec3(entityMat * glm::vec4(curPos, 1.0f));
                        DEBUG_ADD_LINE(viewFamily.debugLines, {prevPos, curPos, kCurveColor, 0.03f});
                        prevPos = curPos;
                    }
                }

                // Draw current progress position
                const int source = component.currentSegment;
                const int target = bIsLoop
                                       ? (source + component.direction + count) % count
                                       : std::clamp(source + component.direction, 0, count - 1);
                glm::vec3 progressPos;
                glm::quat progressRot;
                EvaluatePath(component.controlPoints, source, target, component.progress, bIsLoop, progressPos, progressRot);
                progressPos = glm::vec3(entityMat * glm::vec4(progressPos, 1.0f));
                DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {progressPos, kPointRadius * 1.5f, kProgressColor});
            }
        }
    }

    if (hasGizmoClaim) { state->bCustomGizmoActive = true; }

    return {.requestRemoval = remove};
}
} // Game::Component
