//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_EDITOR_SYSTEMS_H
#define WILL_ENGINE_EDITOR_SYSTEMS_H
#include <entt/entt.hpp>

#include "game/components/common_components.h"
#include "game/components/core_components.h"
#include "game/components/render_components.h"

namespace Engine
{
struct GameState;
}

namespace Core
{
struct FrameBuffer;
struct EngineContext;
}

namespace Game
{
void EditorUpdate(Core::EngineContext* ctx, Engine::GameState* state);
void DrawEditorInterface(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer);

template<typename T>
void DrawComponentEditor(T& component, const Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity) {}

template<> void DrawComponentEditor<Component::TransformComponent>(Component::TransformComponent& component, const Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity);
template<> void DrawComponentEditor<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, const Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity);
template<> void DrawComponentEditor<Component::StableIdComponent>(Component::StableIdComponent& component, const Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity);
template<> void DrawComponentEditor<Component::NameComponent>(Component::NameComponent& component, const Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity);
}

#endif //WILL_ENGINE_EDITOR_SYSTEMS_H
