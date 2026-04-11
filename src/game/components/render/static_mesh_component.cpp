//
// Created by William on 2026-03-21.
//

#include "static_mesh_component.h"

#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "imgui.h"
#include <glm/gtc/type_ptr.hpp>

#include "game/components/scene_components.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/component-registry/component_editor.h"
#include "game/components/core_components.h"
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"

namespace Game::Component
{
void RecreateStaticMesh(StaticMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& runtime = registry.get_or_emplace<MeshRuntime>(entity);

    // Teardown
    for (size_t i = 0; i < runtime.primitives.Size(); ++i) {
        ctx->materialManager->ReleaseMaterial(runtime.primitives[i].materialID);
    }
    runtime.primitives.Clear();
    if (runtime.modelHandle.IsValid()) {
        ctx->assetManager->UnloadModel(runtime.modelHandle);
        runtime.modelHandle = {};
    }

    // Rebuild
    if (component.modelId.IsValid() && component.meshIndex != -1) {
        runtime.modelHandle = ctx->assetManager->LoadModel(component.modelId);
        registry.emplace_or_replace<StaticMeshLoadingTag>(entity);
        state->bPendingModelResolve |= true;
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    registry.emplace_or_replace<DirtyRenderTransformComponent>(entity);
}

void StaticMeshComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<StaticMeshComponent>(entity);
    if (component.meshIndex == -1) {
        return;
    }
    RecreateStaticMesh(component, registry, entity);
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
    return !registry.any_of<Component::ProceduralMeshComponent, Component::SplineMeshComponent>(entity);
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
}

ComponentEditorResult Component::StaticMeshComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry,
                                                                          entt::entity entity, const char* name)
{
    auto& component = registry.get<StaticMeshComponent>(entity);
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

        auto* ctx = registry.ctx().get<Core::EngineContext*>();
        auto* state = registry.ctx().get<Engine::EngineState*>();

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
                component.meshIndex = 0;
                RecreateStaticMesh(component, registry, entity);
            }
            else {
                if (ImGui::BeginCombo("Select Mesh", "")) {
                    for (int32_t i = 0; i < static_cast<int32_t>(model->modelData.meshes.Size()); i++) {
                        const Core::InlineString<>& meshName = model->modelData.meshes[i].name;
                        char fallback[32];
                        if (meshName.Size() == 0) { snprintf(fallback, sizeof(fallback), "Mesh %d", i); }
                        const char* displayName = meshName.Size() > 0 ? meshName.c_str() : fallback;
                        if (ImGui::Selectable(displayName, false)) {
                            component.meshIndex = i;
                            RecreateStaticMesh(component, registry, entity);
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
            std::vector<SlotInfo> slots;
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
                slots.push_back({idx, std::move(slotName)});
            }

            if (!slots.empty() && ImGui::TreeNode("Material Overrides")) {
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
    }

    return {.requestRemoval = remove};
}
} // Game
