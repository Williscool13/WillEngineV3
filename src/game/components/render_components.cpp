//
// Created by William on 2026-01-30.
//

#include "render_components.h"

#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "imgui.h"
#include "scene_components.h"
#include "core/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/systems/editor_systems.h"
#include "game/components/component_initialization.h"
#include "game/scene/scene.h"

namespace Game::Component
{
void StaticMeshComponent::Serialize(const StaticMeshComponent& comp, nlohmann::json& json)
{
    json["meshIndex"] = comp.meshIndex;
    json["modelId"] = comp.modelId.id;
}

void StaticMeshComponent::Deserialize(StaticMeshComponent& comp, const nlohmann::json& json)
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
void DrawComponentEditor<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, const Core::ViewFamily& viewFamily, entt::registry& registry,
                                                         entt::entity entity)
{
    ImGui::Separator();

    bool visible = component.modelFlags.x != 0.0f;
    bool shadowCaster = component.modelFlags.y != 0.0f;
    if (ImGui::Checkbox("Visible", &visible))
        component.modelFlags.x = visible ? 1.0f : 0.0f;
    ImGui::SameLine();
    if (ImGui::Checkbox("Shadow Caster", &shadowCaster))
        component.modelFlags.y = shadowCaster ? 1.0f : 0.0f;

    ImGui::Text("Model ID: %s", component.modelId.ToString());
    ImGui::Text("Mesh Index: %d", component.meshIndex);
    ImGui::Text("Primitive Count: %u", component.primitiveCount);

    if (component.primitiveCount > 0 && ImGui::TreeNode("Primitives")) {
        for (uint8_t i = 0; i < component.primitiveCount; ++i) {
            ImGui::PushID(i);
            if (ImGui::TreeNode("", "Primitive %u", i)) {
                const auto& prim = component.primitives[i];
                ImGui::Text("Primitive Index: %u", prim.primitiveIndex);
                ImGui::Text("Material ID: %u", prim.materialID);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreeNode("Primitives");
        ImGui::TreePop();
    }
}


template<>
void OnComponentAdded<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* scene = registry.try_get<Component::SceneComponent>(entity);
    StringID sceneId = scene ? scene->sceneId : GLOBAL_SCENE_ID;

    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto* state = registry.ctx().get<Engine::GameState*>();
    std::unordered_map<StringID, Engine::WillModelHandle>& sceneModelHandles = state->sceneModelHandles[sceneId];
    if (!sceneModelHandles.contains(component.modelId)) {
        sceneModelHandles[component.modelId] = ctx->assetManager->LoadModel(component.modelId);
    }
    component.modelHandle = sceneModelHandles[component.modelId];
    registry.emplace_or_replace<Component::StaticMeshLoadingTag>(entity);
    state->bPendingModelResolve |= true;

    auto* transform = registry.try_get<Component::TransformComponent>(entity);
    glm::mat4 m = transform ? Component::GetMatrix(*transform) : glm::mat4(1.0f);
    registry.emplace_or_replace<Component::RenderTransformComponent>(entity, m, m);
    registry.emplace_or_replace<Component::DirtyRenderTransformTag>(entity);
}
}
