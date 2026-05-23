//
// Created by William on 2026-03-26.
//

#include "path_mover_component.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <json/nlohmann/json.hpp>

#include "imgui.h"
#include "ImGuizmo.h"

#include "render/interface/render_interface.h"
#include "engine/engine_api.h"
#include "game/component-registry/component_editor.h"
#include "game/component-registry/editor_gizmo_helpers.h"
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

void EvaluatePath(const Engine::Spline& spline, const Core::InlineVector<PathPointSettings, Engine::Spline::MaxPoints>& settings, int32_t source, int32_t target, float t,
                  glm::vec3& outPos, glm::quat& outRot)
{
    if (spline.points.Size() < 2) {
        if (!spline.points.IsEmpty()) { outPos = spline.points[0]; }
        return;
    }

    const PathPointSettings& tgtSettings = (static_cast<size_t>(target) < settings.Size()) ? settings[target] : PathPointSettings{};
    const float easedT = ApplyEasing(tgtSettings.easing, t);

    outPos = spline.EvaluatePosition(source, target, easedT);

    const glm::quat& srcRot = (static_cast<size_t>(source) < settings.Size()) ? settings[source].rotation : glm::quat{1, 0, 0, 0};
    const glm::quat& tgtRot = tgtSettings.rotation;
    outRot = glm::slerp(srcRot, tgtRot, easedT);
}

void PathMoverComponent::Serialize(const PathMoverComponent& comp, nlohmann::json& json)
{
    json["loopMode"] = comp.loopMode;
    Engine::Spline::Serialize(comp.spline, json["spline"]);

    json["pointSettings"] = nlohmann::json::array();
    for (size_t i = 0; i < comp.pointSettings.Size(); i++) {
        const auto& ps = comp.pointSettings[i];
        nlohmann::json psJson;
        psJson["rotation"] = {ps.rotation.x, ps.rotation.y, ps.rotation.z, ps.rotation.w};
        psJson["easing"] = ps.easing;
        psJson["speed"] = ps.speed;
        psJson["waitTime"] = ps.waitTime;
        json["pointSettings"].push_back(psJson);
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

    if (json.contains("spline")) {
        Engine::Spline::Deserialize(comp.spline, json["spline"]);
    }
    comp.spline.bClosed = (comp.loopMode == PathLoopMode::Loop);

    if (json.contains("pointSettings")) {
        for (const auto& psJson : json["pointSettings"]) {
            PathPointSettings ps{};
            if (psJson.contains("rotation")) {
                const auto& r = psJson["rotation"];
                ps.rotation = glm::quat(r[3].get<float>(), r[0].get<float>(), r[1].get<float>(), r[2].get<float>());
            }
            ps.easing = static_cast<EasingType>(psJson.value("easing", 0));
            ps.speed = psJson.value("speed", 1.0f);
            ps.waitTime = psJson.value("waitTime", 0.0f);
            comp.pointSettings.PushBack(ps);
        }
    }

    while (comp.pointSettings.Size() < comp.spline.points.Size()) {
        comp.pointSettings.PushBack({});
    }

    if (comp.spline.points.IsEmpty()) {
        comp.spline.points.PushBack(glm::vec3(0.0f));
        comp.pointSettings.PushBack({});
    }

    comp.currentSegment = json.value("currentSegment", 0);
    comp.progress = json.value("progress", 0.0f);
    comp.direction = json.value("direction", 1);
    comp.bIsWaiting = json.value("bIsWaiting", false);
    comp.waitTimer = json.value("waitTimer", 0.0f);
}

Engine::ComponentEditorResult PathMoverComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& component = registry.get<PathMoverComponent>(entity);
    auto* state = registry.ctx().get<Engine::EngineState*>();

    static int editPointIdx = -1;
    static entt::entity editEntity = entt::null;
    static bool wasUsingGizmo = false;

    if (editEntity != entity) {
        editPointIdx = -1;
        editEntity = entity;
        wasUsingGizmo = false;
    }

    bool hasGizmoClaim = editPointIdx != -1 && !state->editor.bCustomGizmoActive;

    bool open = ImGui::CollapsingHeader("Path Mover", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletepathmover");
    ImGui::PopStyleColor();

    if (open) {
        int loopModeInt = static_cast<int>(component.loopMode);
        if (ImGui::Combo("Loop Mode", &loopModeInt, PathLoopModeNames, static_cast<int>(PathLoopMode::COUNT))) {
            component.loopMode = static_cast<PathLoopMode>(loopModeInt);
            component.spline.bClosed = (component.loopMode == PathLoopMode::Loop);
        }

        int splineModeInt = static_cast<int>(component.spline.mode);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("Spline Mode##pm", &splineModeInt, Engine::SplineModeNames, static_cast<int>(Engine::SplineMode::COUNT))) {
            component.spline.mode = static_cast<Engine::SplineMode>(splineModeInt);
        }

        ImGui::SeparatorText("Runtime State");
        const int maxSeg = std::max(0, component.spline.SegmentCount() - 1);
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
        int pointToSwap = -1;
        const int cpCount = static_cast<int>(component.spline.points.Size());
        for (int i = 0; i < cpCount; i++) {
            ImGui::PushID(i);

            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { pointToSwap = i - 1; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i == cpCount - 1);
            if (ImGui::ArrowButton("##dn", ImGuiDir_Down)) { pointToSwap = i; }
            ImGui::EndDisabled();
            ImGui::SameLine();

            const bool isEditing = (editPointIdx == i);
            ImGui::PushStyleColor(ImGuiCol_Button, isEditing ? Editor::ButtonEditing : Editor::ButtonIdle);
            ImGui::BeginDisabled((state->editor.bCustomGizmoActive || state->editor.bCustomGizmoActivePrev) && !isEditing);
            if (ImGui::SmallButton(isEditing ? "D##edit" : "E##edit")) {
                editPointIdx = isEditing ? -1 : i;
                if (editPointIdx == -1) { hasGizmoClaim = false; }
            }
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            ImGui::SameLine();

            char posLabel[16];
            snprintf(posLabel, sizeof(posLabel), "##pos%d", i);
            ImGui::DragFloat3(posLabel, &component.spline.points[i].x, 0.01f);
            ImGui::SameLine();

            ImGui::BeginDisabled(cpCount <= 1);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::SmallButton("X##rmpt")) {
                pointToRemove = i;
                if (editPointIdx == i) { editPointIdx = -1; }
                else if (editPointIdx > i) { editPointIdx--; }
            }
            ImGui::PopStyleColor();
            ImGui::EndDisabled(); {
                PathPointSettings& ps = (i < static_cast<int>(component.pointSettings.Size())) ? component.pointSettings[i] : component.pointSettings.Back();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);
                char easingLabel[24];
                snprintf(easingLabel, sizeof(easingLabel), "##easing%d", i);
                ImGui::SetNextItemWidth(140.0f);
                int easingInt = static_cast<int>(ps.easing);
                if (ImGui::Combo(easingLabel, &easingInt, EasingTypeNames, static_cast<int>(EasingType::COUNT))) {
                    ps.easing = static_cast<EasingType>(easingInt);
                }
                ImGui::SameLine();
                char speedLabel[24];
                snprintf(speedLabel, sizeof(speedLabel), "##speed%d", i);
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat(speedLabel, &ps.speed, 0.01f, 0.001f, 100.0f, "spd %.2f");
                ImGui::SameLine();
                char waitLabel[24];
                snprintf(waitLabel, sizeof(waitLabel), "##wait%d", i);
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat(waitLabel, &ps.waitTime, 0.05f, 0.0f, 60.0f, "wait %.1fs");
            }

            ImGui::PopID();
        }

        if (pointToRemove >= 0) {
            component.spline.points.RemoveAt(static_cast<size_t>(pointToRemove));
            if (pointToRemove < static_cast<int>(component.pointSettings.Size())) {
                component.pointSettings.RemoveAt(static_cast<size_t>(pointToRemove));
            }
        }
        if (pointToSwap >= 0 && pointToSwap + 1 < cpCount) {
            std::swap(component.spline.points[pointToSwap], component.spline.points[pointToSwap + 1]);
            if (pointToSwap < static_cast<int>(component.pointSettings.Size()) - 1) {
                std::swap(component.pointSettings[pointToSwap], component.pointSettings[pointToSwap + 1]);
            }
            if (editPointIdx == pointToSwap) { editPointIdx = pointToSwap + 1; }
            else if (editPointIdx == pointToSwap + 1) { editPointIdx = pointToSwap; }
        }

        ImGui::BeginDisabled(component.spline.points.IsFull());
        if (ImGui::Button("Add Point")) {
            PathPointSettings newPs{};
            if (component.spline.points.Size() >= 2) {
                const glm::vec3& last = component.spline.points.Back();
                const glm::vec3& prev = component.spline.points[component.spline.points.Size() - 2];
                component.spline.points.PushBack(last + glm::normalize(last - prev));
                newPs = component.pointSettings.IsEmpty() ? PathPointSettings{} : component.pointSettings.Back();
            }
            else if (component.spline.points.Size() == 1) {
                component.spline.points.PushBack(component.spline.points.Back() + glm::vec3(0, 0, 1));
                newPs = component.pointSettings.IsEmpty() ? PathPointSettings{} : component.pointSettings.Back();
            }
            else {
                component.spline.points.PushBack(glm::vec3(0, 0, 0));
            }
            component.pointSettings.PushBack(newPs);
        }
        ImGui::EndDisabled();

        // Gizmo for editing individual control points
        if (editPointIdx == -1) { hasGizmoClaim = false; }
        if (hasGizmoClaim && editPointIdx < cpCount) {
            auto* transform = registry.try_get<TransformComponent>(entity);
            if (transform) {
                const glm::mat4 view = viewFamily.mainView.currentViewData.view;
                const glm::mat4 proj = viewFamily.mainView.currentViewData.proj;
                const glm::mat4 entityMat = glm::translate(glm::mat4(1.0f), transform->translation) * glm::mat4_cast(transform->rotation);
                const glm::mat4 entityMatInv = glm::inverse(entityMat);
                const int idx = editPointIdx;

                glm::vec3 worldPt = glm::vec3(entityMat * glm::vec4(component.spline.points[idx], 1.0f));
                const glm::quat& ptRot = (idx < static_cast<int>(component.pointSettings.Size())) ? component.pointSettings[idx].rotation : glm::quat{1, 0, 0, 0};
                glm::quat worldRot = transform->rotation * ptRot;
                glm::mat4 mat = glm::translate(glm::mat4(1.0f), worldPt) * glm::mat4_cast(worldRot);

                const auto gizmoOp = (state->editor.currentGizmoOperation == ImGuizmo::SCALE)
                                         ? ImGuizmo::TRANSLATE
                                         : state->editor.currentGizmoOperation;

                ImGuizmo::PushID(editPointIdx);
                if (ImGuizmo::Manipulate(
                    glm::value_ptr(view), glm::value_ptr(proj),
                    gizmoOp, ImGuizmo::LOCAL,
                    glm::value_ptr(mat))) {
                    component.spline.points[idx] = glm::vec3(entityMatInv * glm::vec4(glm::vec3(mat[3]), 1.0f));

                    if (gizmoOp == ImGuizmo::ROTATE && idx < static_cast<int>(component.pointSettings.Size())) {
                        glm::mat3 rotMat(mat);
                        rotMat[0] = glm::normalize(rotMat[0]);
                        rotMat[1] = glm::normalize(rotMat[1]);
                        rotMat[2] = glm::normalize(rotMat[2]);
                        component.pointSettings[idx].rotation = glm::inverse(transform->rotation) * glm::quat_cast(rotMat);
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

            for (int i = 0; i < cpCount; i++) {
                glm::vec3 wp = glm::vec3(entityMat * glm::vec4(component.spline.points[i], 1.0f));
                DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {wp, kPointRadius, (i == editPointIdx) ? kEditColor : kPointColor});
            }

            // Draw the actual spline curve
            if (component.spline.points.Size() >= 2) {
                const int numSegments = component.spline.SegmentCount();

                for (int seg = 0; seg < numSegments; seg++) {
                    const int source = seg;
                    const int target = component.spline.bClosed ? (seg + 1) % cpCount : seg + 1;

                    glm::vec3 prevPos = glm::vec3(entityMat * glm::vec4(component.spline.EvaluatePosition(source, target, 0.0f), 1.0f));

                    for (int step = 1; step <= kCurveSubdivisions; step++) {
                        float t = static_cast<float>(step) / static_cast<float>(kCurveSubdivisions);
                        glm::vec3 curPos = glm::vec3(entityMat * glm::vec4(component.spline.EvaluatePosition(source, target, t), 1.0f));
                        DEBUG_ADD_LINE(viewFamily.debugLines, {prevPos, curPos, kCurveColor, 0.03f});
                        prevPos = curPos;
                    }
                }

                // Draw current progress position
                const int source = component.currentSegment;
                const int target = component.spline.bClosed
                                       ? (source + component.direction + cpCount) % cpCount
                                       : std::clamp(source + component.direction, 0, cpCount - 1);
                glm::vec3 progressPos = glm::vec3(entityMat * glm::vec4(component.spline.EvaluatePosition(source, target, component.progress), 1.0f));
                DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {progressPos, kPointRadius * 1.5f, kProgressColor});
            }
        }
    }

    if (hasGizmoClaim) { state->editor.bCustomGizmoActive = true; }

    return {.requestRemoval = remove};
}
} // Game::Component
