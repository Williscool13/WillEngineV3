//
// Created by William on 2026-07-22.
//

#include "reflection_probe_component.h"

#include <imgui.h>
#include <glm/glm.hpp>

#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "engine/components/component_editor.h"
#include "engine/editor/editor_gizmo_helpers.h"
#include "engine/components/core_components.h"
#include "engine/editor/probe_bake_system.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/core/probe_id.h"
#include "engine/input/engine_actions.h"
#include "render/interface/render_interface.h"

namespace Engine::Component
{
static void RequestReflectionProbeLoad(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<ReflectionProbeComponent>(entity);
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();

    registry.remove<ReflectionProbeLoadingTag>(entity);
    const bool bHasBaked = ctx->assetManager->GetProbeInfo(Engine::ProbeID{comp.probeId}) != nullptr;
    if (bHasBaked || comp.standInEnvMap.IsValid()) {
        registry.emplace_or_replace<ReflectionProbeLoadPendingTag>(entity);
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

void ReflectionProbeComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<ReflectionProbeComponent>(entity);
    if (comp.contentSource == ContentSource::Baked) {
        return;
    }
    if (comp.contentHandle.IsValid()) {
        auto* ctx = registry.ctx().get<Engine::EngineContext*>();
        ctx->assetManager->UnloadCubemap(comp.contentHandle);
        comp.contentHandle = Engine::CubemapHandle::INVALID;
    }
    RequestReflectionProbeLoad(registry, entity);
}

Engine::ComponentEditorResult ReflectionProbeComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    static entt::entity editEntity = entt::null;
    static bool bEditing = false;

    if (editEntity != entity || edit.IsMulti()) {
        editEntity = entity;
        bEditing = false;
    }

    auto* state = edit.State();
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
        const auto& comp = edit.Get<ReflectionProbeComponent>();

        EditWidgets::Checkbox(edit, "Enabled##rp", &ReflectionProbeComponent::bEnabled);

        static constexpr const char* SHAPE_LABELS[] = {"Box", "Sphere"};
        EditWidgets::Combo(edit, "Shape##rp", &ReflectionProbeComponent::shape, SHAPE_LABELS, 2);

        EditWidgets::DragFloat(edit, "Fade Margin##rp", &ReflectionProbeComponent::fadeMargin, 0.02f, 0.0f, 10.0f);
        EditWidgets::DragFloat3(edit, "Capture Offset##rp", &ReflectionProbeComponent::captureOffset, 0.05f);
        EditWidgets::Checkbox(edit, "Parallax##rp", &ReflectionProbeComponent::bParallax);

        static constexpr const char* RESOLUTION_LABELS[] = {"128", "256"};
        EditWidgets::Combo(edit, "Resolution##rp", &ReflectionProbeComponent::resolution, RESOLUTION_LABELS, 2);

        auto* ctx = registry.ctx().get<Engine::EngineContext*>();
        const Engine::AssetManager::CachedCubemapMetadata* currentMeta = ctx->assetManager->GetCubemapMetadata(comp.standInEnvMap);
        const char* preview = edit.IsMixed(&ReflectionProbeComponent::standInEnvMap) ? "--" : currentMeta ? currentMeta->name.c_str() : "None";
        if (ImGui::BeginCombo("Stand-in Env Map##rp", preview)) {
            for (const auto& [id, meta] : ctx->assetManager->GetCubemapCache()) {
                if (ImGui::Selectable(meta.name.c_str(), id == comp.standInEnvMap)) {
                    edit.Set(&ReflectionProbeComponent::standInEnvMap, Engine::EnvironmentMapID{id});
                }
            }
            ImGui::EndCombo();
        }
        EditWidgets::DragFloat(edit, "Stand-in Intensity##rp", &ReflectionProbeComponent::standInIntensity, glm::max(comp.standInIntensity * 0.005f, 1.0f), 0.0f, 1.0e9f, "%.0f");

        ProbeBakeSystem& bake = ProbeBakeGet(state);
        const bool bBakeInFlight = bake.bBakeActive && bake.probeEntity == entity;

        ImGui::BeginDisabled(comp.bBakeRequested || bBakeInFlight);
        if (ImGui::Button("Bake##rp")) {
            edit.ForEachTarget<ReflectionProbeComponent>([&registry](entt::entity e) { registry.get<ReflectionProbeComponent>(e).bBakeRequested = true; });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(bake.bBakeActive || bake.bInterbounceBatch || !bake.bakeQueue.IsEmpty());
        if (ImGui::Button("Double Bake##rp")) {
            bake.EnqueueProbeInterbounce(state, entity);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(bake.bBakeActive || bake.bInterbounceBatch || !bake.bakeQueue.IsEmpty());
        if (ImGui::Button("Dry Run##rp")) {
            bake.StartDryRun(ctx, state, entity);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", "Walks the 6 bake faces with the real bake view, hide set, and settle pacing, but captures and writes nothing."); }

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
        ImGui::BeginDisabled(edit.IsMulti() || ((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditing));
        if (ImGui::Button(bEditing ? "Done##rp" : "Edit Bounds##rp")) {
            bEditing = !bEditing;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
    }

    const auto* transform = registry.try_get<TransformComponent>(entity);
    if (transform && bEditing) {
        const auto& comp = edit.Get<ReflectionProbeComponent>();
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
                                                    edit.PreviewSet(&TransformComponent::scale, Vec3(newR));
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
                                                            edit.Preview<TransformComponent>([&](TransformComponent& t) {
                                                                t.scale[i] = newHalf;
                                                                t.translation = opposite + outward * newHalf;
                                                            });
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
                edit.PreviewSet(&ReflectionProbeComponent::captureOffset, Vec3(glm::inverse(rot) * (Vec3(gizmoMat[3]) - center)));
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

    return {.bRequestRemoval = remove};
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
    state->commandQueue.Push({.type = CommandType::ProbeConstruct, .entity = entity});
}

void ReflectionProbeComponent::DeferredConstruct(entt::registry& registry, entt::entity entity)
{
    RequestReflectionProbeLoad(registry, entity);
}

void ReflectionProbeComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<ReflectionProbeComponent>(entity);
    if (comp.contentHandle.IsValid()) {
        auto* state = registry.ctx().get<Engine::EngineState*>();
        state->commandQueue.Push({.type = CommandType::CubemapRelease, .payload = {.cubemapHandle = comp.contentHandle}});
        comp.contentHandle = Engine::CubemapHandle::INVALID;
    }
    registry.remove<ReflectionProbeLoadPendingTag>(entity);
    registry.remove<ReflectionProbeLoadingTag>(entity);
}
}
