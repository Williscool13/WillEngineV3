//
// Created by William on 2026-10-19.
//

#ifndef WILL_ENGINE_COMPONENT_TRAITS_H
#define WILL_ENGINE_COMPONENT_TRAITS_H

#include <concepts>
#include <type_traits>

#include <entt/entt.hpp>

#include "engine/component_registry.h"
#include "engine/components/component_identity.h"

namespace Engine
{
// Shape

template<typename T>
concept TagComponent = std::is_empty_v<T>;

template<typename T>
concept DataComponent = !std::is_empty_v<T>;

// Flags

template<typename T>
concept MovesEntity = requires { { T::MOVES_ENTITY } -> std::convertible_to<bool>; } && T::MOVES_ENTITY;

template<typename T>
concept InstanceOnly = requires { { T::INSTANCE_ONLY } -> std::convertible_to<bool>; } && T::INSTANCE_ONLY;

// Editor hooks

template<typename T>
concept HasDrawEditor = requires(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Engine::EditContext& edit, const char* n) {
    { T::DrawEditor(ctx, state, registry, edit, n) } -> std::same_as<Engine::ComponentEditorResult>;
};

template<typename T>
concept HasCanAdd = requires(const EngineContext* ctx, const EngineState* state, const SystemRegistry& r, entt::entity e) {
    { T::CanAdd(ctx, state, r, e) } -> std::same_as<bool>;
};

template<typename C>
concept HasOnEditPreview = requires(EngineContext* ctx, EngineState* state, SystemRegistry& r, entt::entity e) { C::OnEditPreview(ctx, state, r, e); };

/** Also runs after undo/redo, which never call OnEditPreview, so it must cover everything OnEditPreview does. */
template<typename C>
concept HasOnEditCommit = requires(EngineContext* ctx, EngineState* state, SystemRegistry& r, entt::entity e) { C::OnEditCommit(ctx, state, r, e); };

/** The Has concepts are false on a wrong signature, which would silently skip the hook. */
template<typename C>
void CheckComponentHooks()
{
    if constexpr (requires { &C::DrawEditor; }) { static_assert(HasDrawEditor<C>, "DrawEditor has the wrong signature"); }
    if constexpr (requires { &C::CanAdd; }) { static_assert(HasCanAdd<C>, "CanAdd has the wrong signature"); }
    if constexpr (requires { &C::OnEditPreview; }) { static_assert(HasOnEditPreview<C>, "OnEditPreview has the wrong signature"); }
    if constexpr (requires { &C::OnEditCommit; }) { static_assert(HasOnEditCommit<C>, "OnEditCommit has the wrong signature"); }
}
}

#endif //WILL_ENGINE_COMPONENT_TRAITS_H
