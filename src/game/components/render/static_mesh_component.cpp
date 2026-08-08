//
// Created by William on 2026-03-21.
//

#include "static_mesh_component.h"

#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "imgui.h"
#include <glm/gtc/type_ptr.hpp>

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/component-registry/component_editor.h"
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
    registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
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

void Component::StaticMeshComponent::Serialize(const StaticMeshComponent& comp, nlohmann::json& json)
{
    json["modelId"] = comp.modelId.id;

    nlohmann::json overrides = nlohmann::json::object();
    for (const auto& ov : comp.materialOverrides) {
        auto s = Core::ShortString::Format("%u", ov.slot);
        overrides[s.c_str()] = ov.id.id;
    }
    if (!overrides.empty()) {
        json["materialOverrides"] = std::move(overrides);
    }
    if (!comp.primitiveBlacklist.IsEmpty()) {
        nlohmann::json blacklist = nlohmann::json::array();
        for (uint32_t ord : comp.primitiveBlacklist) { blacklist.push_back(ord); }
        json["primitiveBlacklist"] = std::move(blacklist);
    }
    if (comp.shadingShaderOverride) { json["shadingShaderOverride"] = comp.shadingShaderOverride.id; }
    if (comp.lightingShaderOverride) { json["lightingShaderOverride"] = comp.lightingShaderOverride.id; }
    json["renderOffset"] = {comp.renderOffset.x, comp.renderOffset.y, comp.renderOffset.z};
    json["renderRotation"] = {comp.renderRotation.w, comp.renderRotation.x, comp.renderRotation.y, comp.renderRotation.z};
}

void Component::StaticMeshComponent::Deserialize(StaticMeshComponent& comp, const nlohmann::json& json)
{
    comp.modelId = Engine::ModelID(json["modelId"].get<uint64_t>());

    if (json.contains("materialOverrides")) {
        for (const auto& [key, val] : json["materialOverrides"].items()) {
            comp.SetMaterialOverride(static_cast<uint32_t>(std::stoul(key)), Engine::MaterialID(val.get<uint64_t>()));
        }
    }
    if (json.contains("primitiveBlacklist")) {
        for (const auto& v : json["primitiveBlacklist"]) {
            if (comp.primitiveBlacklist.Size() < StaticMeshComponent::MaxBlacklist) { comp.primitiveBlacklist.PushBack(v.get<uint32_t>()); }
        }
    }
    if (json.contains("shadingShaderOverride")) { comp.shadingShaderOverride = StringID(json["shadingShaderOverride"].get<uint64_t>()); }
    if (json.contains("lightingShaderOverride")) { comp.lightingShaderOverride = StringID(json["lightingShaderOverride"].get<uint64_t>()); }
    if (json.contains("renderOffset")) {
        const auto& o = json["renderOffset"];
        comp.renderOffset = glm::vec3(o[0].get<float>(), o[1].get<float>(), o[2].get<float>());
    }
    if (json.contains("renderRotation")) {
        const auto& r = json["renderRotation"];
        comp.renderRotation = glm::quat(r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>());
    }
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

        auto* runtime = registry.try_get<MeshRuntime>(entity);

        if (!component.modelId.IsValid()) {
            if (ImGui::BeginCombo("Select Model", "")) {
                const auto& modelCache = ctx->assetManager->GetModelCache();
                for (const auto& [key, meta] : modelCache) {
                    if (ImGui::Selectable(meta.name.c_str(), false)) {
                        component.modelId = key;
                        LoadStaticMesh(component, registry, entity);
                    }
                }
                ImGui::EndCombo();
            }
            return {.requestRemoval = remove};
        }

        const auto* modelMeta = ctx->assetManager->GetModelMetadata(component.modelId);
        ImGui::Text("Model: %s", modelMeta ? modelMeta->name.c_str() : "(invalid)");
        ImGui::SameLine();
        if (ImGui::SmallButton("X##deselect_model")) {
            Component::UnloadStaticMesh(registry, entity);
            component.modelId = Engine::ModelID::INVALID;
            registry.remove<RenderTransformComponent>(entity);
            registry.remove<MultiframeDirtyTransformComponent>(entity);
            return {.requestRemoval = remove};
        }

        if (!runtime || !runtime->modelHandle.IsValid()) {
            if (registry.any_of<StaticMeshLoadPendingTag, StaticMeshLoadingTag>(entity)) {
                ImGui::Text("Loading Model...");
                return {.requestRemoval = remove};
            }
            LOG_WARN(Game, "modelId specified but model handle is invalid, resetting to unset");
            component.modelId = Engine::ModelID::INVALID;
            return {.requestRemoval = remove};
        }
        Engine::StaticModel* model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            ImGui::Text("Loading Model...");
            return {.requestRemoval = remove};
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
                return {.requestRemoval = remove};
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
            return {.requestRemoval = remove};
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
                int32_t idx = store[runtime->range.offset + i].originalMaterialIndex;
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
                const auto& allMaterials = ctx->materialManager->GetMaterials();
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

                    if (ImGui::BeginCombo("##override", currentLabel)) {
                        if (ImGui::Selectable("(original)", !current.IsValid())) {
                            if (current.IsValid()) {
                                pendingChangeIdx = slot.origIdx;
                                pendingChangeMat = Engine::MaterialID::INVALID;
                            }
                        }
                        for (const auto& [matId, mat] : allMaterials) {
                            if (mat.immutable) continue;
                            if (ImGui::Selectable(mat.name.c_str(), matId == current)) {
                                if (matId != current) {
                                    pendingChangeIdx = slot.origIdx;
                                    pendingChangeMat = matId;
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::PopID();
                }

                if (pendingChangeIdx >= 0) {
                    component.SetMaterialOverride(static_cast<uint32_t>(pendingChangeIdx), pendingChangeMat);
                    registry.emplace_or_replace<StaticMeshLoadingTag>(entity);
                    state->assetLoad.bPendingModelResolve |= true;
                }

                ImGui::TreePop();
            }
        }

        ImGui::SeparatorText("Shader Overrides");
        {
            auto* pm = ctx->pipelineManager;
            Core::Span<const StringID> shadingPipelines = pm->GetShadingPipelines();
            Core::Span<const StringID> lightingPipelines = pm->GetLightingPipelines();
            Core::Arena& arena = ctx->editorArena.Get();

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
            auto* rt = registry.try_get<RenderTransformComponent>(entity);
            if (rt) {
                rt->renderOffset = component.renderOffset;
                registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
            }
        }

        // Rotation row
        glm::vec3 renderEuler = glm::degrees(glm::eulerAngles(component.renderRotation));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Rotation");
        ImGui::SameLine(labelColW);
        if (drawXYZ("##rrx", "##rry", "##rrz", &renderEuler.x, 0.5f, bEditingOffset)) {
            component.renderRotation = glm::quat(glm::radians(renderEuler));
            auto* rt = registry.try_get<RenderTransformComponent>(entity);
            if (rt) {
                rt->renderRotation = component.renderRotation;
                registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
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
                component.renderOffset = Vec3(entityMatInv * Vec4(Vec3(gizmoMat[3]), 1.0f));
                component.renderRotation = glm::inverse(world.rotation) * glm::quat_cast(Mat3(gizmoMat));
                auto* rt = registry.try_get<RenderTransformComponent>(entity);
                if (rt) {
                    rt->renderOffset = component.renderOffset;
                    rt->renderRotation = component.renderRotation;
                    registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
                }
            }
            if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) { state->editor.bExclusiveGizmoActive = true; }
            ImGuizmo::PopID();
            ImGuizmo::SetGizmoSizeClipSpace(0.1f);
        }
    }

    return {.requestRemoval = remove};
}
} // Game
