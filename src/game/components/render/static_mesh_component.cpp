//
// Created by William on 2026-03-21.
//

#include "static_mesh_component.h"

#include <entt/entt.hpp>

#include "imgui.h"
#include <glm/gtc/type_ptr.hpp>

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "game/component-registry/component_editor.h"
#include "game/editor/editor_materials.h"
#include "game/component-registry/editor_gizmo_helpers.h"
#include <ImGuizmo.h>

#include "core/containers/arena_array.h"
#include "game/components/core_components.h"
#include "game/systems/scene_system.h"
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"
#include "game/components/render/text3d_component.h"

namespace Game::Component
{
void UnloadStaticMesh(entt::registry& registry, entt::entity entity)
{
    registry.remove<MeshRuntime>(entity);
    registry.remove<StaticMeshLoadPendingTag>(entity);
    registry.remove<StaticMeshLoadingTag>(entity);
}

void LoadStaticMesh(StaticMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();

    registry.remove<StaticMeshLoadingTag>(entity);
    if (component.modelId.IsValid()) {
        registry.emplace_or_replace<StaticMeshLoadPendingTag>(entity);
        state->assetLoad.bPendingModelResolve |= true;
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    rt.renderRotation = component.renderRotation;
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
}

Engine::MaterialID StaticMeshComponent::GetMaterialOverride(uint32_t slot) const
{
    for (const auto& ov : materialOverrides) {
        if (ov.slot == slot) { return ov.id; }
    }
    return Engine::MaterialID::INVALID;
}

void StaticMeshComponent::SetMaterialOverride(uint32_t slot, Engine::MaterialID id)
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
}
}


namespace Game
{
bool Component::StaticMeshComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return !registry.any_of<ProceduralMeshComponent, SplineMeshComponent, Text3DComponent>(entity);
}

void Component::StaticMeshComponent::Serialize(const StaticMeshComponent& comp, Engine::TextWriter& w)
{
    static const StaticMeshComponent DEF{};
    w.Key("modelId", comp.modelId.id);

    if (!comp.materialOverrides.IsEmpty()) {
        w.Count("materialOverrides", static_cast<uint32_t>(comp.materialOverrides.Size()));
        for (const auto& ov : comp.materialOverrides) {
            w.BeginBlock("m");
            w.Key("slot", ov.slot);
            w.Key("id", ov.id.id);
            w.EndBlock();
        }
    }
    if (!comp.primitiveBlacklist.IsEmpty()) {
        w.KeyUInts("primitiveBlacklist", comp.primitiveBlacklist.Data(), comp.primitiveBlacklist.Size());
    }
    w.KeyOpt("shadingShaderOverride", comp.shadingShaderOverride.id, uint64_t{0});
    w.KeyOpt("lightingShaderOverride", comp.lightingShaderOverride.id, uint64_t{0});
    w.KeyOpt("renderOffset", comp.renderOffset, DEF.renderOffset);
    w.KeyOpt("renderRotation", comp.renderRotation, DEF.renderRotation);
}

void Component::StaticMeshComponent::Deserialize(StaticMeshComponent& comp, const Engine::TextReader& r)
{
    comp.modelId = Engine::ModelID(r.U64("modelId", comp.modelId.id));

    r.ForEachRecord("materialOverrides", [&](const Engine::TextReader& m) {
        comp.SetMaterialOverride(m.UInt("slot"), Engine::MaterialID(m.U64("id")));
    });
    r.ForEachUInt("primitiveBlacklist", [&](uint32_t ord) {
        if (comp.primitiveBlacklist.Size() < MaxBlacklist) { comp.primitiveBlacklist.PushBack(ord); }
    });
    comp.shadingShaderOverride = StringID(r.U64("shadingShaderOverride", comp.shadingShaderOverride.id));
    comp.lightingShaderOverride = StringID(r.U64("lightingShaderOverride", comp.lightingShaderOverride.id));
    comp.renderOffset = r.Vec3("renderOffset", comp.renderOffset);
    comp.renderRotation = r.Quat("renderRotation", comp.renderRotation);
}

Engine::ComponentEditorResult Component::StaticMeshComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry,
                                                                         entt::entity entity, const char* name)
{
    static entt::entity editEntity = entt::null;
    static bool bEditingOffset = false;

    if (editEntity != entity) {
        editEntity = entity;
        bEditingOffset = false;
    }

    auto& component = registry.get<StaticMeshComponent>(entity);
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();

    if (bEditingOffset) { state->editor.bExclusiveGizmoActive = true; }

    bool open = ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletestaticmesh");
    ImGui::PopStyleColor();

    bool modified = false;
    if (open) {
        auto& renderFlags = registry.get_or_emplace<RenderFlagsComponent>(entity);
        bool visible = renderFlags.Has(RenderFlagsComponent::VISIBLE);
        bool ddgiContribution = renderFlags.Has(RenderFlagsComponent::DDGI_CONTRIBUTE);
        if (ImGui::Checkbox("Visible", &visible)) {
            renderFlags.Set(RenderFlagsComponent::VISIBLE, visible);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("DDGI Contribution", &ddgiContribution)) {
            renderFlags.Set(RenderFlagsComponent::DDGI_CONTRIBUTE, ddgiContribution);
        }
        bool probeBakeExclude = !renderFlags.Has(RenderFlagsComponent::PROBE_BAKE_INCLUDE);
        if (ImGui::Checkbox("Probe Bake Exclude", &probeBakeExclude)) {
            renderFlags.Set(RenderFlagsComponent::PROBE_BAKE_INCLUDE, !probeBakeExclude);
        }
        ImGui::SameLine();
        bool motionBlurExclude = !renderFlags.Has(RenderFlagsComponent::MOTION_BLUR);
        if (ImGui::Checkbox("Motion Blur Exclude", &motionBlurExclude)) {
            renderFlags.Set(RenderFlagsComponent::MOTION_BLUR, !motionBlurExclude);
        }
        ImGui::SameLine();
        bool alphaCutoutExclude = !renderFlags.Has(RenderFlagsComponent::ALPHA_CUTOUT);
        if (ImGui::Checkbox("Alpha Cutout Exclude", &alphaCutoutExclude)) {
            renderFlags.Set(RenderFlagsComponent::ALPHA_CUTOUT, !alphaCutoutExclude);
        }
        bool emissiveLight = renderFlags.Has(RenderFlagsComponent::EMISSIVE_LIGHT);
        if (ImGui::Checkbox("Emissive Light", &emissiveLight)) {
            renderFlags.Set(RenderFlagsComponent::EMISSIVE_LIGHT, emissiveLight);
            registry.emplace_or_replace<StaticMeshLoadingTag>(entity);
            state->assetLoad.bPendingModelResolve = true;
            modified = true;
        }

        auto* runtime = registry.try_get<MeshRuntime>(entity);

        if (!component.modelId.IsValid()) {
            if (ImGui::BeginCombo("Select Model", "")) {
                const auto& modelCache = ctx->assetManager->GetModelCache();
                for (const auto& [key, meta] : modelCache) {
                    if (ImGui::Selectable(meta.name.c_str(), false)) {
                        component.modelId = key;
                        LoadStaticMesh(component, registry, entity);
                        modified = true;
                    }
                }
                ImGui::EndCombo();
            }
            return {.bRequestRemoval = remove, .bModified = modified};
        }

        const auto* modelMeta = ctx->assetManager->GetModelMetadata(component.modelId);
        ImGui::Text("Model: %s", modelMeta ? modelMeta->name.c_str() : "(invalid)");
        ImGui::SameLine();
        if (ImGui::SmallButton("X##deselect_model")) {
            Component::UnloadStaticMesh(registry, entity);
            component.modelId = Engine::ModelID::INVALID;
            registry.remove<RenderTransformComponent>(entity);
            registry.remove<MultiframeDirtyComponent>(entity);
            return {.bRequestRemoval = remove, .bModified = true};
        }

        if (!runtime || !runtime->modelHandle.IsValid()) {
            if (registry.any_of<StaticMeshLoadPendingTag, StaticMeshLoadingTag>(entity)) {
                ImGui::Text("Loading Model...");
                return {.bRequestRemoval = remove};
            }
            LOG_WARN(Game, "modelId specified but model handle is invalid, resetting to unset");
            component.modelId = Engine::ModelID::INVALID;
            return {.bRequestRemoval = remove, .bModified = true};
        }
        Engine::StaticModel* model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            ImGui::Text("Loading Model...");
            return {.bRequestRemoval = remove};
        }

        Engine::InstanceStore& store = state->instanceStore;
        const uint32_t primCount = runtime->range.count;
        ImGui::Text("Primitive Count: %u", primCount);

        if (!component.primitiveBlacklist.IsEmpty()) {
            ImGui::Text("Split off: %u", static_cast<uint32_t>(component.primitiveBlacklist.Size()));
            ImGui::SameLine();
            if (ImGui::SmallButton("Restore##hidden")) {
                component.primitiveBlacklist.Clear();
                registry.emplace_or_replace<StaticMeshLoadingTag>(entity);
                state->assetLoad.bPendingModelResolve |= true;
                return {.bRequestRemoval = remove, .bModified = true};
            }
        }

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
                    if (ImGui::SmallButton("Split Off")) {
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
            return {.bRequestRemoval = remove, .bModified = true};
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

                    Engine::MaterialID current = component.GetMaterialOverride(static_cast<uint32_t>(slot.origIdx));
                    const char* currentLabel = "(original)";
                    if (current.IsValid()) {
                        if (const Engine::Material* m = ctx->materialManager->GetMaterial(current)) {
                            currentLabel = m->name.c_str();
                        }
                    }

                    ImGui::Text("%s", slot.name.c_str());
                    ImGui::SameLine();

                    if (ImGui::BeginCombo("##override", currentLabel, ImGuiComboFlags_HeightLarge)) {
                        if (ImGui::Selectable("(original)", !current.IsValid())) {
                            if (current.IsValid()) {
                                pendingChangeIdx = slot.origIdx;
                                pendingChangeMat = Engine::MaterialID::INVALID;
                            }
                        }
                        const Engine::MaterialID picked = Game::DrawMaterialSelector(ctx, state, state->editor.materialSelector, current);
                        if (picked.IsValid() && picked != current) {
                            pendingChangeIdx = slot.origIdx;
                            pendingChangeMat = picked;
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::PopID();
                }

                if (pendingChangeIdx >= 0) {
                    component.SetMaterialOverride(static_cast<uint32_t>(pendingChangeIdx), pendingChangeMat);
                    registry.emplace_or_replace<StaticMeshLoadingTag>(entity);
                    state->assetLoad.bPendingModelResolve |= true;
                    modified = true;
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

            bool shaderChanged = false;

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
                    shaderChanged = true;
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
                    shaderChanged = true;
                }
            }

            if (shaderChanged) {
                registry.emplace_or_replace<StaticMeshLoadingTag>(entity);
                state->assetLoad.bPendingModelResolve |= true;
                modified = true;
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
        if (drawXYZ("##rox", "##roy", "##roz", &component.renderOffset.x, 0.1f, bEditingOffset)) {
            modified = true;
            auto* rt = registry.try_get<RenderTransformComponent>(entity);
            if (rt) {
                rt->renderOffset = component.renderOffset;
                registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
            }
        }

        // Rotation row
        glm::vec3 renderEuler = glm::degrees(glm::eulerAngles(component.renderRotation));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Rotation");
        ImGui::SameLine(labelColW);
        if (drawXYZ("##rrx", "##rry", "##rrz", &renderEuler.x, 0.5f, bEditingOffset)) {
            modified = true;
            component.renderRotation = glm::quat(glm::radians(renderEuler));
            auto* rt = registry.try_get<RenderTransformComponent>(entity);
            if (rt) {
                rt->renderRotation = component.renderRotation;
                registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
            }
        }

        ImGui::PushStyleColor(ImGuiCol_Button, bEditingOffset ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
        ImGui::BeginDisabled((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditingOffset);
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
                modified = true;
                component.renderOffset = Vec3(entityMatInv * Vec4(Vec3(gizmoMat[3]), 1.0f));
                component.renderRotation = glm::inverse(world.rotation) * glm::quat_cast(Mat3(gizmoMat));
                auto* rt = registry.try_get<RenderTransformComponent>(entity);
                if (rt) {
                    rt->renderOffset = component.renderOffset;
                    rt->renderRotation = component.renderRotation;
                    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
                }
            }
            if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) { state->editor.bExclusiveGizmoActive = true; }
            ImGuizmo::PopID();
            ImGuizmo::SetGizmoSizeClipSpace(0.1f);
        }
    }

    return {.bRequestRemoval = remove, .bModified = modified};
}
} // Game
