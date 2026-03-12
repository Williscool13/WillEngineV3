//
// Created by William on 2026-01-30.
//

#include "render_components.h"

#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "imgui.h"
#include "component_copy.h"
#include "component_serialization.h"
#include "scene_components.h"
#include "core/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/systems/editor_systems.h"
#include "game/components/component_initialization.h"

namespace Game
{
template<>
Component::StaticMeshComponent CopyComponent(const Component::StaticMeshComponent& src, entt::registry& dstReg)
{
    Component::StaticMeshComponent copy{};
    copy.modelFlags = src.modelFlags;
    copy.modelId = src.modelId;
    copy.meshIndex = src.meshIndex;
    return copy;
}

template<>
void SerializeComponent<Component::StaticMeshComponent>(const Component::StaticMeshComponent& comp, nlohmann::json& json)
{
    json["meshIndex"] = comp.meshIndex;
    json["modelId"] = comp.modelId.id;
}

template<>
void DeserializeComponent<Component::StaticMeshComponent>(Component::StaticMeshComponent& comp, const nlohmann::json& json)
{
    const auto& mi = json["meshIndex"];
    comp.meshIndex = mi.get<int32_t>();

    const auto& mdi = json["modelId"];
    comp.modelId = StringID(mdi.get<uint64_t>());
}
}

namespace Game
{
template<>
ComponentEditorResult DrawComponentEditor<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry,
                                                                          entt::entity entity, const char* name)
{
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
        auto* state = registry.ctx().get<Engine::GameState*>();

        if (component.modelId == StringID::Invalid) {
            if (ImGui::BeginCombo("Select Model", "")) {
                const std::unordered_map<StringID, std::filesystem::path>& modelReg = ctx->assetManager->GetModelRegistry();
                for (const auto& key : modelReg | std::views::keys) {
                    if (ImGui::Selectable(key.ToString(), false)) {
                        component.modelId = key;
                        component.modelHandle = ctx->assetManager->LoadModel(component.modelId);
                    }
                }
                ImGui::EndCombo();
            }
            return {.requestRemoval = remove};
        }

        ImGui::Text("Model ID: %s", component.modelId.ToString());
        ImGui::SameLine();
        if (ImGui::SmallButton("X##deselect_model")) {
            ctx->assetManager->UnloadModel(component.modelHandle);
            component.modelId = StringID::Invalid;
            component.modelHandle = {};
            component.meshIndex = -1;
            component.primitiveCount = 0;
            registry.remove<Component::StaticMeshLoadingTag>(entity);
            registry.remove<Component::RenderTransformComponent>(entity);
            registry.remove<Component::DirtyRenderTransformComponent>(entity);
            return {.requestRemoval = remove};
        }

        assert(component.modelHandle.IsValid() && "modelId specified but model handle is still invalid");
        Engine::StaticModel* model = ctx->assetManager->GetModel(component.modelHandle);
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            ImGui::Text("Loading Model...");
            return {.requestRemoval = remove};
        }

        if (component.meshIndex == -1) {
            if (model->modelData.meshes.size() == 1) {
                component.meshIndex = 0;
                registry.emplace_or_replace<Component::StaticMeshLoadingTag>(entity);
                auto* transform = registry.try_get<Component::TransformComponent>(entity);
                glm::mat4 m = transform ? Component::GetMatrix(*transform) : glm::mat4(1.0f);
                registry.emplace_or_replace<Component::RenderTransformComponent>(entity, m, m);
                registry.emplace_or_replace<Component::DirtyRenderTransformComponent>(entity);
                state->bPendingModelResolve |= true;
            }
            else {
                if (ImGui::BeginCombo("Select Mesh", "")) {
                    for (int32_t i = 0; i < model->modelData.meshes.size(); i++) {
                        auto _name = model->modelData.meshes[i].name;
                        if (_name.empty()) {
                            _name = fmt::format("Mesh {}", i);
                        }
                        if (ImGui::Selectable(_name.c_str(), false)) {
                            component.meshIndex = i;
                            registry.emplace_or_replace<Component::StaticMeshLoadingTag>(entity);
                            auto* transform = registry.try_get<Component::TransformComponent>(entity);
                            glm::mat4 m = transform ? Component::GetMatrix(*transform) : glm::mat4(1.0f);
                            registry.emplace_or_replace<Component::RenderTransformComponent>(entity, m, m);
                            registry.emplace_or_replace<Component::DirtyRenderTransformComponent>(entity);
                            state->bPendingModelResolve |= true;
                        }
                    }
                    ImGui::EndCombo();
                }
                return {.requestRemoval = remove};
            }
        }

        ImGui::Text("Mesh Index: %d", component.meshIndex);
        if (model->modelData.meshes.size() > 1) {
            if (ImGui::SmallButton("X##deselect_mesh")) {
                component.meshIndex = -1;
                component.primitiveCount = 0;
                registry.remove<Component::StaticMeshLoadingTag>(entity);
                registry.remove<Component::RenderTransformComponent>(entity);
                registry.remove<Component::DirtyRenderTransformComponent>(entity);
                return {.requestRemoval = remove};
            }
        }

        ImGui::Text("Primitive Count: %u", component.primitiveCount);

        if (component.primitiveCount > 0 && ImGui::TreeNode("Primitives")) {
            for (uint8_t i = 0; i < component.primitiveCount; ++i) {
                ImGui::PushID(i);
                if (ImGui::TreeNode("", "Primitive %u", i)) {
                    const auto& prim = component.primitives[i];
                    ImGui::Text("Primitive Index: %u", prim.primitiveIndex);
                    ImGui::Text("Material ID: %llu", prim.materialID.id);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }

    return {.requestRemoval = remove};
}

template<>
void OnComponentAdded<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    if (component.meshIndex == -1) {
        return;
    }

    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto* state = registry.ctx().get<Engine::GameState*>();
    component.modelHandle = ctx->assetManager->LoadModel(component.modelId);
    registry.emplace_or_replace<Component::StaticMeshLoadingTag>(entity);
    state->bPendingModelResolve |= true;

    auto* transform = registry.try_get<Component::TransformComponent>(entity);
    glm::mat4 m = transform ? Component::GetMatrix(*transform) : glm::mat4(1.0f);
    registry.emplace_or_replace<Component::RenderTransformComponent>(entity, m, m);
    registry.emplace_or_replace<Component::DirtyRenderTransformComponent>(entity);
}

template<>
void OnComponentRemoved<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    for (size_t i = 0; i < component.primitiveCount; ++i) {
        ctx->materialManager->ReleaseMaterial(component.primitives[i].materialID);
    }
    if (component.modelHandle.IsValid()) {
        ctx->assetManager->UnloadModel(component.modelHandle);
    }

    registry.remove<Component::StaticMeshLoadingTag>(entity);
    registry.remove<Component::RenderTransformComponent>(entity);
    registry.remove<Component::DirtyRenderTransformComponent>(entity);
    registry.remove<Component::StaticMeshComponent>(entity);
}
}
