//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_EDITOR_SYSTEMS_H
#define WILL_ENGINE_EDITOR_SYSTEMS_H
#include <entt/entt.hpp>
#include <fmt/format.h>
#include "imgui.h"

#include "game/components/common_components.h"
#include "game/components/component_types.h"
#include "game/components/core_components.h"
#include "game/components/physics_components.h"
#include "game/components/render_components.h"

namespace Engine
{
struct GameState;
}

namespace Core
{
struct FrameBuffer;
struct EngineContext;
struct ViewFamily;
}

namespace Game
{
void EditorUpdate(Core::EngineContext* ctx, Engine::GameState* state);

void DrawEditorInterface(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer);

template<typename T>
ComponentEditorResult DrawComponentEditor(T& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton(fmt::format("X##{}", name).c_str());
    ImGui::PopStyleColor();
    return {.requestRemoval = remove};
}

template<>
ComponentEditorResult DrawComponentEditor<Component::TransformComponent>(Component::TransformComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

template<>
ComponentEditorResult DrawComponentEditor<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

template<>
ComponentEditorResult DrawComponentEditor<Component::ProceduralMeshComponent>(Component::ProceduralMeshComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

template<>
ComponentEditorResult DrawComponentEditor<Component::StableIdComponent>(Component::StableIdComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

template<>
ComponentEditorResult DrawComponentEditor<Component::NameComponent>(Component::NameComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

template<>
ComponentEditorResult DrawComponentEditor<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

template<>
ComponentEditorResult DrawComponentEditor<Component::DrawPhysicsDebugTag>(Component::DrawPhysicsDebugTag& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
}

#endif //WILL_ENGINE_EDITOR_SYSTEMS_H
