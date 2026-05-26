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
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"

namespace Game::Component
{
void UnloadStaticMesh(StaticMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto& runtime = registry.get_or_emplace<MeshRuntime>(entity);

    // Materials acquired when the model has resolved. If it never resolves this is a no-op
    for (size_t i = 0; i < runtime.primitives.Size(); ++i) {
        ctx->materialManager->ReleaseMaterial(runtime.primitives[i].materialID);
    }
    runtime.primitives.Clear();

    if (runtime.modelHandle.IsValid()) {
        ctx->assetManager->UnloadModel(runtime.modelHandle);
        runtime.modelHandle = {};
    }

    // If unloaded before it finished loading
    registry.remove<StaticMeshLoadingTag>(entity);
}

void LoadStaticMesh(StaticMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& runtime = registry.get_or_emplace<MeshRuntime>(entity);

    if (component.modelId.IsValid() && component.meshIndex != -1) {
        runtime.modelHandle = ctx->assetManager->LoadModel(component.modelId);
        registry.emplace_or_replace<StaticMeshLoadingTag>(entity);
        state->bPendingModelResolve |= true;
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    rt.renderRotation = component.renderRotation;
    registry.emplace_or_replace<DirtyRenderTransformComponent>(entity);
}

void StaticMeshComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<StaticMeshComponent>(entity);
    if (component.meshIndex == -1) {
        return;
    }
    LoadStaticMesh(component, registry, entity);
}

void StaticMeshComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    registry.remove<MeshRuntime>(entity);
    registry.remove<StaticMeshLoadingTag>(entity);
    registry.remove<RenderTransformComponent>(entity);
    registry.remove<DirtyRenderTransformComponent>(entity);
}
}


namespace Game
{
bool Component::StaticMeshComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return !registry.any_of<ProceduralMeshComponent, SplineMeshComponent>(entity);
}

void Component::StaticMeshComponent::Serialize(const StaticMeshComponent& comp, nlohmann::json& json)
{
    json["meshIndex"] = comp.meshIndex;
    json["modelId"] = comp.modelId.id;
    json["modelFlags"] = {comp.modelFlags.x, comp.modelFlags.y, comp.modelFlags.z, comp.modelFlags.w};

    nlohmann::json overrides = nlohmann::json::object();
    for (int32_t i = 0; i < 128; ++i) {
        if (comp.materialOverrides[i].IsValid()) {
            auto s = Core::ShortString::Format("%d", i);
            overrides[s.c_str()] = comp.materialOverrides[i].id;
        }
    }
    if (!overrides.empty()) {
        json["materialOverrides"] = std::move(overrides);
    }
    if (comp.shadingShaderOverride) { json["shadingShaderOverride"] = comp.shadingShaderOverride.id; }
    if (comp.lightingShaderOverride) { json["lightingShaderOverride"] = comp.lightingShaderOverride.id; }
    json["renderOffset"] = {comp.renderOffset.x, comp.renderOffset.y, comp.renderOffset.z};
    json["renderRotation"] = {comp.renderRotation.w, comp.renderRotation.x, comp.renderRotation.y, comp.renderRotation.z};
}

void Component::StaticMeshComponent::Deserialize(StaticMeshComponent& comp, const nlohmann::json& json)
{
    comp.meshIndex = json["meshIndex"].get<int32_t>();
    comp.modelId = Engine::ModelID(json["modelId"].get<uint64_t>());
    if (json.contains("modelFlags")) {
        const auto& f = json["modelFlags"];
        comp.modelFlags = glm::vec4(f[0].get<float>(), f[1].get<float>(), f[2].get<float>(), f[3].get<float>());
    }

    if (json.contains("materialOverrides")) {
        for (const auto& [key, val] : json["materialOverrides"].items()) {
            int32_t idx = std::stoi(key);
            if (idx >= 0 && idx < 128) {
                comp.materialOverrides[idx] = Engine::MaterialID(val.get<uint64_t>());
            }
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
        bool visible = component.modelFlags.x != 0.0f;
        bool shadowCaster = component.modelFlags.y != 0.0f;
        if (ImGui::Checkbox("Visible", &visible)) {
            component.modelFlags.x = visible ? 1.0f : 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Shadow Caster", &shadowCaster)) {
            component.modelFlags.y = shadowCaster ? 1.0f : 0.0f;
        }

        auto* runtime = registry.try_get<MeshRuntime>(entity);

        if (!component.modelId.IsValid()) {
            if (ImGui::BeginCombo("Select Model", "")) {
                const auto& modelCache = ctx->assetManager->GetModelCache();
                for (const auto& [key, meta] : modelCache) {
                    if (ImGui::Selectable(meta.name.c_str(), false)) {
                        component.modelId = key;
                        auto& rt = registry.get_or_emplace<MeshRuntime>(entity);
                        rt.modelHandle = ctx->assetManager->LoadModel(component.modelId);
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
            if (runtime && runtime->modelHandle.IsValid()) {
                ctx->assetManager->UnloadModel(runtime->modelHandle);
                runtime->modelHandle = {};
                runtime->primitives.Clear();
            }
            component.modelId = Engine::ModelID::INVALID;
            component.meshIndex = -1;
            registry.remove<StaticMeshLoadingTag>(entity);
            registry.remove<RenderTransformComponent>(entity);
            registry.remove<DirtyRenderTransformComponent>(entity);
            return {.requestRemoval = remove};
        }

        if (!runtime || !runtime->modelHandle.IsValid()) {
            LOG_WARN(Game, "modelId specified but model handle is invalid, resetting to unset");
            component.modelId = Engine::ModelID::INVALID;
            return {.requestRemoval = remove};
        }
        Engine::StaticModel* model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            ImGui::Text("Loading Model...");
            return {.requestRemoval = remove};
        }

        if (component.meshIndex == -1) {
            if (model->modelData.meshes.Size() == 1) {
                UnloadStaticMesh(component, registry, entity);
                component.meshIndex = 0;
                LoadStaticMesh(component, registry, entity);
            }
            else {
                if (ImGui::BeginCombo("Select Mesh", "")) {
                    for (int32_t i = 0; i < static_cast<int32_t>(model->modelData.meshes.Size()); i++) {
                        const Core::InlineString<>& meshName = model->modelData.meshes[i].name;
                        char fallback[32];
                        if (meshName.Size() == 0) { snprintf(fallback, sizeof(fallback), "Mesh %d", i); }
                        const char* displayName = meshName.Size() > 0 ? meshName.c_str() : fallback;
                        if (ImGui::Selectable(displayName, false)) {
                            UnloadStaticMesh(component, registry, entity);
                            component.meshIndex = i;
                            LoadStaticMesh(component, registry, entity);
                        }
                    }
                    ImGui::EndCombo();
                }
                return {.requestRemoval = remove};
            }
        }

        ImGui::Text("Mesh Index: %d", component.meshIndex);
        if (model->modelData.meshes.Size() > 1) {
            if (ImGui::SmallButton("X##deselect_mesh")) {
                component.meshIndex = -1;
                if (runtime) runtime->primitives.Clear();
                registry.remove<StaticMeshLoadingTag>(entity);
                registry.remove<RenderTransformComponent>(entity);
                registry.remove<DirtyRenderTransformComponent>(entity);
                return {.requestRemoval = remove};
            }
        }

        const size_t primCount = runtime ? runtime->primitives.Size() : 0;
        ImGui::Text("Primitive Count: %zu", primCount);

        if (primCount > 0 && ImGui::TreeNode("Primitives")) {
            for (size_t i = 0; i < primCount; ++i) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::TreeNode("", "Primitive %zu", i)) {
                    const auto& prim = runtime->primitives[i];
                    ImGui::Text("Primitive Index: %u", prim.primitiveIndex);
                    ImGui::Text("Material ID: %llu", prim.materialID.id);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        if (primCount > 0) {
            struct SlotInfo
            {
                int32_t origIdx;
                Core::InlineString<128> name;
            };
            Core::InlineVector<SlotInfo, 128> slots;
            bool seen[128] = {};
            for (size_t i = 0; i < primCount; ++i) {
                int32_t idx = runtime->primitives[i].originalMaterialIndex;
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

                    Engine::MaterialID current = component.materialOverrides[slot.origIdx];
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
                    component.materialOverrides[pendingChangeIdx] = pendingChangeMat;
                    registry.emplace_or_replace<StaticMeshLoadingTag>(entity);
                    state->bPendingModelResolve |= true;
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
                state->bPendingModelResolve |= true;
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
            c |= drawField(idX, v + 0, Editor::ColorAxisX, speed, editable);
            ImGui::SameLine(0, innerSpacing);
            c |= drawField(idY, v + 1, Editor::ColorAxisY, speed, editable);
            ImGui::SameLine(0, innerSpacing);
            c |= drawField(idZ, v + 2, Editor::ColorAxisZ, speed, editable);
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
                registry.emplace_or_replace<DirtyRenderTransformComponent>(entity);
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
                registry.emplace_or_replace<DirtyRenderTransformComponent>(entity);
            }
        }

        ImGui::PushStyleColor(ImGuiCol_Button, bEditingOffset ? Editor::ButtonEditing : Editor::ButtonIdle);
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
            const Mat4 entityMat = GetMatrix(*transform);
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
            ImGuizmo::PushID(9900);
            const Quat worldRenderRot = transform->rotation * component.renderRotation;
            Mat4 gizmoMat = glm::translate(Mat4(1.0f), pivotWorld) * glm::mat4_cast(worldRenderRot);
            if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), state->editor.currentGizmoOperation, state->editor.currentGizmoMode, glm::value_ptr(gizmoMat), nullptr, snap)) {
                component.renderOffset = Vec3(entityMatInv * Vec4(Vec3(gizmoMat[3]), 1.0f));
                component.renderRotation = glm::inverse(transform->rotation) * glm::quat_cast(Mat3(gizmoMat));
                auto* rt = registry.try_get<RenderTransformComponent>(entity);
                if (rt) {
                    rt->renderOffset = component.renderOffset;
                    rt->renderRotation = component.renderRotation;
                    registry.emplace_or_replace<DirtyRenderTransformComponent>(entity);
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
