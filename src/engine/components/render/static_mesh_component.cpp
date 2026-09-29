//
// Created by William on 2026-03-21.
//

#include "static_mesh_component.h"

#include <entt/entt.hpp>

#include "imgui.h"
#include <glm/gtc/type_ptr.hpp>

#include "mesh_source_exclusion.h"

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "engine/components/component_editor.h"
#include "engine/editor/editor_materials.h"
#include "engine/editor/editor_gizmo_helpers.h"
#include <ImGuizmo.h>

#include "core/containers/arena_array.h"
#include "engine/components/core_components.h"
#include "engine/systems/scene_system.h"
#include "engine/components/render/procedural_mesh_component.h"
#include "engine/components/render/spline_mesh_component.h"
#include "engine/components/render/text3d_component.h"

namespace Engine::Component
{
void UnloadStaticMesh(entt::registry& registry, entt::entity entity)
{
    registry.remove<MeshRuntime>(entity);
    registry.remove<StaticMeshLoadPendingTag>(entity);
    registry.remove<StaticMeshLoadingTag>(entity);
}

void LoadStaticMesh(StaticMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    registry.remove<StaticMeshLoadingTag>(entity);
    if (component.modelId.IsValid()) {
        registry.emplace_or_replace<StaticMeshLoadPendingTag>(entity);
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    rt.renderRotation = component.renderRotation;
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
}

void PruneStaticMeshOverrides(entt::registry& registry, entt::entity entity)
{
    const auto* overrides = registry.try_get<StaticMeshOverridesComponent>(entity);
    if (overrides && overrides->materialOverrides.IsEmpty() && overrides->primitiveBlacklist.IsEmpty()) {
        registry.remove<StaticMeshOverridesComponent>(entity);
    }
}

Engine::MaterialID StaticMeshOverridesComponent::GetMaterialOverride(uint32_t slot) const
{
    for (const auto& ov : materialOverrides) {
        if (ov.slot == slot) { return ov.id; }
    }
    return Engine::MaterialID::INVALID;
}

void StaticMeshOverridesComponent::SetMaterialOverride(uint32_t slot, Engine::MaterialID id)
{
    for (size_t i = 0; i < materialOverrides.Size(); ++i) {
        if (materialOverrides[i].slot == slot) {
            if (id.IsValid()) { materialOverrides[i].id = id; }
            else { materialOverrides.SwapRemove(i); }
            return;
        }
    }
    if (id.IsValid() && materialOverrides.Size() < MaxMaterialOverrides) {
        materialOverrides.PushBack({slot, id});
    }
}

void StaticMeshComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    registry.get_or_emplace<RenderFlagsComponent>(entity);
    auto& component = registry.get<StaticMeshComponent>(entity);
    if (!component.modelId.IsValid()) {
        return;
    }
    LoadStaticMesh(component, registry, entity);
}

void StaticMeshComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    UnloadStaticMesh(registry, entity);
    registry.remove<RenderTransformComponent>(entity);
    registry.remove<StaticMeshOverridesComponent>(entity);
}
}

namespace Engine
{
bool Component::StaticMeshComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return MeshSources::NoneOtherThan<StaticMeshComponent>(registry, entity);
}

bool Component::StaticMeshOverridesComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return registry.all_of<StaticMeshComponent>(entity);
}



void Component::StaticMeshComponent::OnEditPreview(entt::registry& registry, entt::entity entity)
{
    const auto& component = registry.get<StaticMeshComponent>(entity);
    if (auto* rt = registry.try_get<RenderTransformComponent>(entity)) {
        rt->renderOffset = component.renderOffset;
        rt->renderRotation = component.renderRotation;
        registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
    }
}

void Component::StaticMeshComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<StaticMeshComponent>(entity);
    if (!component.modelId.IsValid()) {
        UnloadStaticMesh(registry, entity);
        registry.remove<RenderTransformComponent>(entity);
        registry.remove<MultiframeDirtyComponent>(entity);
        return;
    }
    if (!registry.all_of<RenderTransformComponent>(entity)) {
        LoadStaticMesh(component, registry, entity);
        return;
    }
    registry.emplace_or_replace<StaticMeshLoadingTag>(entity);
    OnEditPreview(registry, entity);
}

void Component::StaticMeshOverridesComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    registry.emplace_or_replace<StaticMeshLoadingTag>(entity);
    PruneStaticMeshOverrides(registry, entity);
}

Engine::ComponentEditorResult Component::StaticMeshComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    static entt::entity editEntity = entt::null;
    static bool bEditingOffset = false;

    if (editEntity != entity || edit.IsMulti()) {
        editEntity = entity;
        bEditingOffset = false;
    }

    const StaticMeshComponent before = edit.Get<StaticMeshComponent>();
    StaticMeshComponent component = before;
    bool bCommit = false;
    auto finish = [&](ComponentEditorResult result) {
        edit.PreviewDiff(before, component);
        if (bCommit) {
            edit.Commit<StaticMeshComponent>();
        }
        return result;
    };
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = edit.State();

    if (bEditingOffset) { state->editor.bExclusiveGizmoActive = true; }

    bool open = ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletestaticmesh");
    ImGui::PopStyleColor();

    if (open) {
        if (DrawRenderFlagToggles(edit, RENDER_TOGGLE_ALL)) {
            edit.ForEachTarget<StaticMeshComponent>([&registry](entt::entity e) { registry.emplace_or_replace<StaticMeshLoadingTag>(e); });
        }

        auto* runtime = registry.try_get<MeshRuntime>(entity);

        if (!component.modelId.IsValid()) {
            if (ImGui::BeginCombo("Select Model", "")) {
                const auto& modelCache = ctx->assetManager->GetModelCache();
                for (const auto& [key, meta] : modelCache) {
                    if (ImGui::Selectable(meta.name.c_str(), false)) {
                        component.modelId = key;
                        bCommit = true;
                    }
                }
                ImGui::EndCombo();
            }
            return finish({.bRequestRemoval = remove});
        }

        const auto* modelMeta = ctx->assetManager->GetModelMetadata(component.modelId);
        ImGui::Text("Model: %s", edit.IsMixed(&StaticMeshComponent::modelId) ? "--" : modelMeta ? modelMeta->name.c_str() : "(invalid)");
        ImGui::SameLine();
        if (ImGui::SmallButton("X##deselect_model")) {
            component.modelId = Engine::ModelID::INVALID;
            bCommit = true;
            return finish({.bRequestRemoval = remove});
        }

        if (!runtime || !runtime->modelHandle.IsValid()) {
            if (registry.any_of<StaticMeshLoadPendingTag, StaticMeshLoadingTag>(entity)) {
                ImGui::Text("Loading Model...");
                return finish({.bRequestRemoval = remove});
            }
            LOG_WARN(Engine, "modelId specified but model handle is invalid, resetting to unset");
            registry.get<StaticMeshComponent>(entity).modelId = Engine::ModelID::INVALID;
            return {.bRequestRemoval = remove};
        }
        Engine::StaticModel* model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            ImGui::Text("Loading Model...");
            return finish({.bRequestRemoval = remove});
        }

        Engine::InstanceStore& store = state->instanceStore;
        const uint32_t primCount = runtime->range.count;
        ImGui::Text("Primitive Count: %u", primCount);

        const auto* overrides = registry.try_get<StaticMeshOverridesComponent>(entity);

        if (overrides && !overrides->primitiveBlacklist.IsEmpty()) {
            ImGui::Text("Split off: %u", static_cast<uint32_t>(overrides->primitiveBlacklist.Size()));
            ImGui::SameLine();
            if (ImGui::SmallButton("Restore##hidden")) {
                edit.Modify<StaticMeshOverridesComponent>([](StaticMeshOverridesComponent& o) { o.primitiveBlacklist.Clear(); });
                return finish({.bRequestRemoval = remove});
            }
        }

        ImGui::BeginDisabled(edit.IsMulti());
        if (primCount > 0 && ImGui::SmallButton("Split All")) {
            ImGui::EndDisabled();
            SplitAllMeshPrimitives(ctx, state, entity);
            return {.bRequestRemoval = true};
        }
        ImGui::EndDisabled();

        uint32_t pendingSplitOrdinal = ~0u;
        glm::mat4 pendingSplitTransform{1.0f};
        if (primCount > 0 && ImGui::TreeNode("Primitives")) {
            for (uint32_t i = 0; i < primCount; ++i) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::TreeNode("", "Primitive %u", i)) {
                    const auto& prim = store[runtime->range.offset + i];
                    ImGui::Text("Primitive Index: %u", prim.primitiveIndex);
                    ImGui::Text("Node: %u", prim.sourceNodeIndex);
                    ImGui::Text("Material ID: %llu", prim.materialID.id);
                    if (!edit.IsMulti() && ImGui::SmallButton("Split Off")) {
                        pendingSplitOrdinal = prim.modelPrimitiveOrdinal;
                        pendingSplitTransform = prim.modelSpaceTransform;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        if (pendingSplitOrdinal != ~0u) {
            SplitOffMeshPrimitive(state, entity, pendingSplitOrdinal, pendingSplitTransform);
            return finish({.bRequestRemoval = remove});
        }

        if (primCount > 0) {
            struct SlotInfo
            {
                int32_t origIdx;
                Core::InlineString<128> name;
            };
            Core::InlineVector<SlotInfo, 128> slots;
            bool seen[128] = {};
            for (uint32_t i = 0; i < primCount; ++i) {
                int32_t idx = store[runtime->range.offset + i].materialSlot;
                if (idx < 0 || idx >= 128 || seen[idx]) continue;
                seen[idx] = true;
                Core::InlineString<128> slotName;
                if (idx < static_cast<int32_t>(model->modelData.materials.Size()) &&
                    !model->modelData.materials[idx].name.IsEmpty()) {
                    slotName = model->modelData.materials[idx].name;
                }
                else {
                    slotName = Core::InlineString<128>::Format("Material %d", idx);
                }
                slots.PushBack({idx, std::move(slotName)});
            }

            if (!slots.IsEmpty() && ImGui::TreeNode("Material Overrides")) {
                int32_t pendingChangeIdx = -1;
                Engine::MaterialID pendingChangeMat{};

                for (const auto& slot : slots) {
                    ImGui::PushID(slot.origIdx);

                    Engine::MaterialID current = overrides ? overrides->GetMaterialOverride(static_cast<uint32_t>(slot.origIdx)) : Engine::MaterialID::INVALID;
                    const char* currentLabel = "__synthesized__";
                    if (current.IsValid()) {
                        if (const Engine::Material* m = ctx->materialManager->GetMaterial(current)) {
                            currentLabel = m->name.c_str();
                        }
                    }

                    ImGui::Text("%s", slot.name.c_str());
                    ImGui::SameLine();

                    if (ImGui::BeginCombo("##override", currentLabel, ImGuiComboFlags_HeightLarge)) {
                        if (ImGui::Selectable("__synthesized__", !current.IsValid())) {
                            if (current.IsValid()) {
                                pendingChangeIdx = slot.origIdx;
                                pendingChangeMat = Engine::MaterialID::INVALID;
                            }
                        }
                        const Engine::MaterialID picked = Engine::DrawMaterialSelector(ctx, state, state->editor.materialSelector, current);
                        if (picked.IsValid() && picked != current) {
                            pendingChangeIdx = slot.origIdx;
                            pendingChangeMat = picked;
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::PopID();
                }

                if (pendingChangeIdx >= 0) {
                    edit.ForEachTarget<StaticMeshComponent>([&registry](entt::entity e) { registry.get_or_emplace<StaticMeshOverridesComponent>(e); });
                    edit.Modify<StaticMeshOverridesComponent>([pendingChangeIdx, pendingChangeMat](StaticMeshOverridesComponent& o) {
                        o.SetMaterialOverride(static_cast<uint32_t>(pendingChangeIdx), pendingChangeMat);
                    });
                }

                ImGui::TreePop();
            }
        }

        ImGui::SeparatorText("Shader Overrides");
        {
            auto* pm = ctx->pipelineManager;
            Core::Span<const StringID> shadingPipelines = pm->GetShadingPipelines();
            Core::Arena& arena = ctx->editorArena.Get();
            Core::ArenaFixedVector<StringID> lightingPipelines = pm->GetLightingPipelinesForMode(viewFamily.lightingMode, arena);

            {
                const int32_t pipelineCount = static_cast<int32_t>(shadingPipelines.Size());
                int32_t current = -1;
                for (int32_t i = 0; i < pipelineCount; ++i) {
                    if (component.shadingShaderOverride == shadingPipelines[i]) { current = i; break; }
                }
                Core::ArenaArray<Core::InlineString<64>> labels(&arena, pipelineCount + 1);
                labels[0] = Core::InlineString<64>("(none)");
                for (int32_t i = 0; i < pipelineCount; ++i) { labels[i + 1] = Core::InlineString<64>(shadingPipelines[i].ToString()); }
                int32_t comboIdx = current + 1;
                auto getter = [](void* data, int idx) -> const char* { return (*static_cast<Core::ArenaArray<Core::InlineString<64>>*>(data))[idx].c_str(); };
                if (ImGui::Combo("Shading", &comboIdx, getter, &labels, static_cast<int32_t>(labels.Size()))) {
                    component.shadingShaderOverride = comboIdx == 0 ? StringID{} : shadingPipelines[comboIdx - 1];
                    bCommit = true;
                }
            }

            {
                const int32_t pipelineCount = static_cast<int32_t>(lightingPipelines.Size());
                int32_t current = -1;
                for (int32_t i = 0; i < pipelineCount; ++i) {
                    if (component.lightingShaderOverride == lightingPipelines[i]) { current = i; break; }
                }
                Core::ArenaArray<Core::InlineString<64>> labels(&arena, pipelineCount + 1);
                labels[0] = Core::InlineString<64>("(none)");
                for (int32_t i = 0; i < pipelineCount; ++i) { labels[i + 1] = Core::InlineString<64>(lightingPipelines[i].ToString()); }
                int32_t comboIdx = current + 1;
                auto getter = [](void* data, int idx) -> const char* { return (*static_cast<Core::ArenaArray<Core::InlineString<64>>*>(data))[idx].c_str(); };
                if (ImGui::Combo("Lighting", &comboIdx, getter, &labels, static_cast<int32_t>(labels.Size()))) {
                    component.lightingShaderOverride = comboIdx == 0 ? StringID{} : lightingPipelines[comboIdx - 1];
                    bCommit = true;
                }
            }
        }

        ImGui::SeparatorText("Render Transform");

        const float innerSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float outerSpacing = ImGui::GetStyle().ItemSpacing.x;
        const float frameRounding = ImGui::GetStyle().FrameRounding;
        const float labelColW = ImGui::CalcTextSize("Rotation").x + outerSpacing * 3.0f;
        const float fieldW = (ImGui::GetContentRegionAvail().x - labelColW - innerSpacing * 2.0f) / 3.0f;
        const float fieldH = ImGui::GetFrameHeight();
        constexpr float stripW = 4.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto drawField = [&](const char* id, float* val, ImU32 strip, float speed, bool editable) -> bool {
            ImGui::SetNextItemWidth(fieldW);
            ImGui::BeginDisabled(!editable);
            bool changed = ImGui::DragFloat(id, val, speed, 0, 0, "%.2f");
            bCommit |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::EndDisabled();
            ImVec2 p = ImGui::GetItemRectMin();
            dl->AddRectFilled(p, {p.x + stripW, p.y + fieldH}, strip, frameRounding, ImDrawFlags_RoundCornersLeft);
            return changed;
        };
        auto drawXYZ = [&](const char* idX, const char* idY, const char* idZ, float* v, float speed, bool editable) -> bool {
            bool c = false;
            c |= drawField(idX, v + 0, Editor::COLOR_AXIS_X, speed, editable);
            ImGui::SameLine(0, innerSpacing);
            c |= drawField(idY, v + 1, Editor::COLOR_AXIS_Y, speed, editable);
            ImGui::SameLine(0, innerSpacing);
            c |= drawField(idZ, v + 2, Editor::COLOR_AXIS_Z, speed, editable);
            return c;
        };

        // Offset row
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Offset");
        ImGui::SameLine(labelColW);
        drawXYZ("##rox", "##roy", "##roz", &component.renderOffset.x, 0.1f, bEditingOffset);

        // Rotation row
        glm::vec3 renderEuler = glm::degrees(glm::eulerAngles(component.renderRotation));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Rotation");
        ImGui::SameLine(labelColW);
        if (drawXYZ("##rrx", "##rry", "##rrz", &renderEuler.x, 0.5f, bEditingOffset)) {
            component.renderRotation = glm::quat(glm::radians(renderEuler));
        }

        ImGui::BeginDisabled(edit.IsMulti());
        bCommit |= Editor::MeshPivotPresets(ctx, registry, entity, component.renderRotation, component.renderOffset);
        ImGui::EndDisabled();

        ImGui::PushStyleColor(ImGuiCol_Button, bEditingOffset ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
        ImGui::BeginDisabled(edit.IsMulti() || ((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditingOffset));
        if (ImGui::Button(bEditingOffset ? "Done##offsetedit" : "Edit##offsetedit")) {
            bEditingOffset = !bEditingOffset;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
    }

    if (bEditingOffset) {
        auto* transform = registry.try_get<TransformComponent>(entity);
        if (transform) {
            const auto& world = registry.get<WorldTransformComponent>(entity);
            const Mat4 entityMat = GetMatrix(world);
            const Mat4 entityMatInv = glm::inverse(entityMat);
            const Vec3 pivotWorld = Vec3(entityMat * Vec4(component.renderOffset, 1.0f));

            const Mat4 view = viewFamily.mainView.currentViewData.view;
            const Mat4 proj = viewFamily.mainView.currentViewData.proj;

            float snapArr[3] = {};
            float* snap = nullptr;
            if (state->editor.bSnapEnabled) {
                if (state->editor.currentGizmoOperation == ImGuizmo::TRANSLATE) {
                    snapArr[0] = snapArr[1] = snapArr[2] = state->editor.snapTranslation;
                } else if (state->editor.currentGizmoOperation == ImGuizmo::ROTATE) {
                    snapArr[0] = snapArr[1] = snapArr[2] = state->editor.snapRotation;
                } else {
                    snapArr[0] = snapArr[1] = snapArr[2] = state->editor.snapScale;
                }
                snap = snapArr;
            }

            ImGuizmo::SetGizmoSizeClipSpace(0.10f);
            ImGuizmo::PushID(Editor::GizmoId::STATIC_MESH_TRANSFORM);
            const Quat worldRenderRot = world.rotation * component.renderRotation;
            Mat4 gizmoMat = glm::translate(Mat4(1.0f), pivotWorld) * glm::mat4_cast(worldRenderRot);
            if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), state->editor.currentGizmoOperation, state->editor.currentGizmoMode, glm::value_ptr(gizmoMat), nullptr, snap)) {
                component.renderOffset = Vec3(entityMatInv * Vec4(Vec3(gizmoMat[3]), 1.0f));
                component.renderRotation = glm::inverse(world.rotation) * glm::quat_cast(Mat3(gizmoMat));
            }
            if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) { state->editor.bExclusiveGizmoActive = true; }
            ImGuizmo::PopID();
            ImGuizmo::SetGizmoSizeClipSpace(0.1f);
        }
    }

    return finish({.bRequestRemoval = remove});
}
} // Engine
