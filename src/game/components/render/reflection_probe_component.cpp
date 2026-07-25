//
// Created by William on 2026-07-22.
//

#include "reflection_probe_component.h"

#include <imgui.h>
#include <glm/glm.hpp>

#include "game/component-registry/component_editor.h"
#include "game/component-registry/editor_gizmo_helpers.h"
#include "game/component-registry/json_helpers.h"
#include "game/components/core_components.h"
#include "game/systems/probe_bake_system.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/core/probe_id.h"
#include "game/input/game_actions.h"
#include "render/interface/render_interface.h"

namespace Game::Component
{
static void RequestReflectionProbeLoad(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<ReflectionProbeComponent>(entity);
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();

    registry.remove<ReflectionProbeLoadingTag>(entity);
    const bool bHasBaked = ctx->assetManager->GetProbeInfo(Engine::ProbeID{comp.probeId}) != nullptr;
    if (bHasBaked || comp.standInEnvMap.IsValid()) {
        registry.emplace_or_replace<ReflectionProbeLoadPendingTag>(entity);
        state->bPendingModelResolve |= true;
    }
}

bool ReflectionProbeComponent::IsBakeStale(const WorldTransformComponent& world, const ReflectionProbeComponent& comp, const Engine::ProbeBakeSnapshot& snap, uint32_t bakedResolutionPx)
{
    constexpr float kEpsilon = 1e-3f;

    if (glm::abs(world.translation.x - snap.translation[0]) > kEpsilon) { return true; }
    if (glm::abs(world.translation.y - snap.translation[1]) > kEpsilon) { return true; }
    if (glm::abs(world.translation.z - snap.translation[2]) > kEpsilon) { return true; }

    if (glm::abs(world.scale.x - snap.scale[0]) > kEpsilon) { return true; }
    if (glm::abs(world.scale.y - snap.scale[1]) > kEpsilon) { return true; }
    if (glm::abs(world.scale.z - snap.scale[2]) > kEpsilon) { return true; }

    glm::vec4 liveRot{world.rotation.w, world.rotation.x, world.rotation.y, world.rotation.z};
    glm::vec4 bakedRot{snap.rotation[0], snap.rotation[1], snap.rotation[2], snap.rotation[3]};
    if (glm::dot(liveRot, bakedRot) < 0.0f) { bakedRot = -bakedRot; }
    if (glm::abs(liveRot.x - bakedRot.x) > kEpsilon) { return true; }
    if (glm::abs(liveRot.y - bakedRot.y) > kEpsilon) { return true; }
    if (glm::abs(liveRot.z - bakedRot.z) > kEpsilon) { return true; }
    if (glm::abs(liveRot.w - bakedRot.w) > kEpsilon) { return true; }

    if (glm::abs(comp.captureOffset.x - snap.captureOffset[0]) > kEpsilon) { return true; }
    if (glm::abs(comp.captureOffset.y - snap.captureOffset[1]) > kEpsilon) { return true; }
    if (glm::abs(comp.captureOffset.z - snap.captureOffset[2]) > kEpsilon) { return true; }

    const uint32_t livePixels = comp.resolution == ReflectionProbeComponent::Resolution::Res128 ? 128u : 256u;
    if (livePixels != bakedResolutionPx) { return true; }

    return false;
}

Engine::ComponentEditorResult ReflectionProbeComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    static entt::entity editEntity = entt::null;
    static bool bEditing = false;

    if (editEntity != entity) {
        editEntity = entity;
        bEditing = false;
    }

    auto* state = registry.ctx().get<Engine::EngineState*>();
    if (bEditing) {
        state->editor.bExclusiveGizmoActive = true;
        const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (!popupOpen && state->input.GetActionState(Actions::ACTION_ESCAPE).down) {
            bEditing = false;
        }
    }

    bool open = ImGui::CollapsingHeader("Reflection Probe", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletereflectionprobe");
    ImGui::PopStyleColor();

    if (open) {
        auto& comp = registry.get<ReflectionProbeComponent>(entity);

        ImGui::Checkbox("Enabled##rp", &comp.bEnabled);

        static constexpr const char* SHAPE_LABELS[] = {"Box", "Sphere"};
        int shapeIndex = static_cast<int>(comp.shape);
        if (ImGui::Combo("Shape##rp", &shapeIndex, SHAPE_LABELS, 2)) {
            comp.shape = static_cast<Shape>(shapeIndex);
        }

        ImGui::DragFloat("Fade Margin##rp", &comp.fadeMargin, 0.02f, 0.0f, 10.0f);
        ImGui::DragFloat3("Capture Offset##rp", &comp.captureOffset.x, 0.05f);
        ImGui::Checkbox("Parallax##rp", &comp.bParallax);

        static constexpr const char* RESOLUTION_LABELS[] = {"128", "256"};
        int resolutionIndex = static_cast<int>(comp.resolution);
        if (ImGui::Combo("Resolution##rp", &resolutionIndex, RESOLUTION_LABELS, 2)) {
            comp.resolution = static_cast<Resolution>(resolutionIndex);
        }

        auto* ctx = registry.ctx().get<Engine::EngineContext*>();
        const Engine::AssetManager::CachedCubemapMetadata* currentMeta = ctx->assetManager->GetCubemapMetadata(comp.standInEnvMap);
        const char* preview = currentMeta ? currentMeta->name.c_str() : "None";
        if (ImGui::BeginCombo("Stand-in Env Map##rp", preview)) {
            for (const auto& [id, meta] : ctx->assetManager->GetCubemapCache()) {
                const bool selected = id == comp.standInEnvMap;
                if (ImGui::Selectable(meta.name.c_str(), selected) && id != comp.standInEnvMap) {
                    if (comp.contentHandle.IsValid()) {
                        ctx->assetManager->UnloadCubemap(comp.contentHandle);
                        comp.contentHandle = Engine::CubemapHandle::INVALID;
                    }
                    comp.standInEnvMap = id;
                    RequestReflectionProbeLoad(registry, entity);
                }
            }
            ImGui::EndCombo();
        }

        ProbeBakeSystem& bake = ProbeBakeGetOrCreate(state);
        const bool bBakeInFlight = bake.bBakeActive && bake.probeEntity == entity;

        ImGui::BeginDisabled(comp.bBakeRequested || bBakeInFlight);
        if (ImGui::Button("Bake##rp")) {
            comp.bBakeRequested = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(bake.bBakeActive || bake.bInterbounceBatch || !bake.bakeQueue.IsEmpty());
        if (ImGui::Button("Double Bake##rp")) {
            bake.EnqueueProbeInterbounce(state, entity);
        }
        ImGui::EndDisabled();

        const char* sourceLabel = "None";
        switch (comp.contentSource) {
            case ContentSource::Baked: { sourceLabel = "Baked"; break; }
            case ContentSource::StandIn: { sourceLabel = "Stand-in"; break; }
            case ContentSource::None: { sourceLabel = "None"; break; }
        }
        ImGui::Text("Content: %s", sourceLabel);

        const Engine::AssetManager::ProbeInfo* probeInfo = ctx->assetManager->GetProbeInfo(Engine::ProbeID{comp.probeId});
        if (!probeInfo) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Not baked");
        } else if (const auto* worldTransform = registry.try_get<WorldTransformComponent>(entity); worldTransform && IsBakeStale(*worldTransform, comp, probeInfo->snapshot, probeInfo->resolution)) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Bake is stale (transform changed). Rebake.");
        }

        ImGui::PushStyleColor(ImGuiCol_Button, bEditing ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
        ImGui::BeginDisabled((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditing);
        if (ImGui::Button(bEditing ? "Done##rp" : "Edit Bounds##rp")) {
            bEditing = !bEditing;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    if (transform && bEditing) {
        auto& comp = registry.get<ReflectionProbeComponent>(entity);
        auto* ctx = registry.ctx().get<Engine::EngineContext*>();
        const auto& vd = viewFamily.mainView.currentViewData;
        const Vec3 center = transform->translation;
        const Quat rot = transform->rotation;
        const bool bSphere = comp.shape == Shape::Sphere;

        const Vec4 viewport{
            static_cast<float>(ctx->windowContext.viewportOffsetX),
            static_cast<float>(ctx->windowContext.viewportOffsetY),
            static_cast<float>(ctx->windowContext.viewportWidth),
            static_cast<float>(ctx->windowContext.viewportHeight),
        };

        bool bHandleHot = false;
        if (bSphere) {
            const Vec3 right = rot * Vec3(1.0f, 0.0f, 0.0f);
            const float r = glm::max(glm::max(transform->scale.x, transform->scale.y), transform->scale.z);
            bHandleHot |= Editor::AxisDotHandle(Editor::DotHandleId::REFLECTION_PROBE_BOUNDS_BASE, center + right * r, right,
                                                vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                [&](Vec3 newPt) {
                                                    const float newR = glm::max(0.05f, glm::dot(newPt - center, right));
                                                    transform->scale = Vec3(newR);
                                                    registry.emplace_or_replace<DirtyTransformTag>(entity);
                                                },
                                                Editor::COLOR_AXIS_X);
        }
        else {
            const Vec3 axes[3] = {rot * Vec3(1.0f, 0.0f, 0.0f), rot * Vec3(0.0f, 1.0f, 0.0f), rot * Vec3(0.0f, 0.0f, 1.0f)};
            const ImU32 colors[3] = {Editor::COLOR_AXIS_X, Editor::COLOR_AXIS_Y, Editor::COLOR_AXIS_Z};
            for (int i = 0; i < 3; ++i) {
                for (int s = 0; s < 2; ++s) {
                    const float sign = s == 0 ? 1.0f : -1.0f;
                    const Vec3 outward = axes[i] * sign;
                    const int32_t handleId = Editor::DotHandleId::REFLECTION_PROBE_BOUNDS_BASE + i * 2 + s;
                    const Vec3 handlePos = center + outward * transform->scale[i];

                    bHandleHot |= Editor::AxisDotHandle(handleId, handlePos, outward,
                                                        vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                        [&](Vec3 newPt) {
                                                            const Vec3 opposite = center - outward * transform->scale[i];
                                                            const float newExtentFull = glm::dot(newPt - opposite, outward);
                                                            const float newHalf = glm::max(0.05f, newExtentFull * 0.5f);
                                                            transform->scale[i] = newHalf;
                                                            transform->translation = opposite + outward * newHalf;
                                                            registry.emplace_or_replace<DirtyTransformTag>(entity);
                                                        },
                                                        colors[i]);
                }
            }
        }

        const Vec3 capturePos = center + rot * comp.captureOffset;
        // Face dots take input priority; skipping Manipulate while one is hot stops the gizmo's arrows from stealing clicks aimed at a handle.
        if (!bHandleHot) {
            float snapArr[3] = {};
            float* snap = nullptr;
            if (state->editor.bSnapEnabled) {
                snapArr[0] = snapArr[1] = snapArr[2] = state->editor.snapTranslation;
                snap = snapArr;
            }
            ImGuizmo::SetGizmoSizeClipSpace(0.10f);
            ImGuizmo::PushID(Editor::GizmoId::REFLECTION_PROBE_CAPTURE);
            Mat4 gizmoMat = glm::translate(Mat4(1.0f), capturePos) * glm::mat4_cast(rot);
            if (ImGuizmo::Manipulate(glm::value_ptr(vd.view), glm::value_ptr(vd.proj), ImGuizmo::TRANSLATE, state->editor.currentGizmoMode, glm::value_ptr(gizmoMat), nullptr, snap)) {
                comp.captureOffset = glm::inverse(rot) * (Vec3(gizmoMat[3]) - center);
            }
            if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) { state->editor.bExclusiveGizmoActive = true; }
            ImGuizmo::PopID();
            ImGuizmo::SetGizmoSizeClipSpace(0.1f);
        }

        if (comp.fadeMargin > 0.0f) {
            constexpr Vec4 shellColor{0.95f, 0.9f, 0.35f, 1.0f};
            const float lineWidth = state->projectConfig.reflectionProbeLineWidth;
            if (bSphere) {
                const float r = glm::max(glm::max(transform->scale.x, transform->scale.y), transform->scale.z);
                const float inner = glm::max(0.01f, r - comp.fadeMargin);
                DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {center, inner, shellColor, lineWidth});
            }
            else {
                const Vec3 innerExtents{
                    glm::max(0.01f, transform->scale.x - comp.fadeMargin),
                    glm::max(0.01f, transform->scale.y - comp.fadeMargin),
                    glm::max(0.01f, transform->scale.z - comp.fadeMargin),
                };
                DEBUG_ADD_BOX(viewFamily.debugBoxes, {center, innerExtents, rot, shellColor, lineWidth});
            }
        }
    }

    return {.requestRemoval = remove};
}

void ReflectionProbeComponent::Serialize(const ReflectionProbeComponent& comp, nlohmann::json& json)
{
    json["probeId"] = comp.probeId;
    json["bEnabled"] = comp.bEnabled;
    json["shape"] = static_cast<uint32_t>(comp.shape);
    json["fadeMargin"] = comp.fadeMargin;
    json["captureOffset"] = {comp.captureOffset.x, comp.captureOffset.y, comp.captureOffset.z};
    json["bParallax"] = comp.bParallax;
    json["resolution"] = static_cast<uint32_t>(comp.resolution);
    json["standInEnvMap"] = comp.standInEnvMap.id;
}

void ReflectionProbeComponent::Deserialize(ReflectionProbeComponent& comp, const nlohmann::json& json)
{
    if (!json.is_object()) { return; }
    comp.probeId = json.value("probeId", uint64_t{0});
    comp.bEnabled = json.value("bEnabled", true);
    comp.shape = static_cast<Shape>(json.value("shape", static_cast<uint32_t>(Shape::Box)));
    comp.fadeMargin = json.value("fadeMargin", 0.5f);
    comp.captureOffset = json.contains("captureOffset") ? json["captureOffset"].get<Vec3>() : Vec3{0.0f, 0.0f, 0.0f};
    comp.bParallax = json.value("bParallax", true);
    comp.resolution = static_cast<Resolution>(json.value("resolution", static_cast<uint32_t>(Resolution::Res256)));
    comp.standInEnvMap = Engine::EnvironmentMapID{json.value("standInEnvMap", uint64_t{0})};
}

void ReflectionProbeComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<ReflectionProbeComponent>(entity);
    auto* state = registry.ctx().get<Engine::EngineState*>();

    comp.contentHandle = Engine::CubemapHandle::INVALID;
    comp.contentSource = ContentSource::None;
    comp.bBakeRequested = false;
    if (comp.probeId == 0) {
        comp.probeId = state->rng();
    }
    RequestReflectionProbeLoad(registry, entity);
}

void ReflectionProbeComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<ReflectionProbeComponent>(entity);
    if (comp.contentHandle.IsValid()) {
        auto* ctx = registry.ctx().get<Engine::EngineContext*>();
        ctx->assetManager->UnloadCubemap(comp.contentHandle);
        comp.contentHandle = Engine::CubemapHandle::INVALID;
    }
    registry.remove<ReflectionProbeLoadPendingTag>(entity);
    registry.remove<ReflectionProbeLoadingTag>(entity);
}
}
