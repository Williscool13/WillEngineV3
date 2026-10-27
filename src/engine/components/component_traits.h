//
// Created by William on 2026-10-19.
//

#ifndef WILL_ENGINE_COMPONENT_TRAITS_H
#define WILL_ENGINE_COMPONENT_TRAITS_H

#include <concepts>
#include <type_traits>

#include <entt/entt.hpp>

#include "engine/component_registry.h"

namespace Engine
{
// Identity

template<typename T>
concept NamedComponent = requires {
    { T::COMPONENT_NAME } -> std::convertible_to<const char*>;
};

template<typename T>
concept DomainComponent = NamedComponent<T> && requires {
    { T::COMPONENT_DOMAIN } -> std::convertible_to<const char*>;
};

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
concept HasDrawEditor = requires(Core::ViewFamily& vf, Engine::EditContext& edit, const char* n) {
    { T::DrawEditor(vf, edit, n) } -> std::same_as<Engine::ComponentEditorResult>;
};

template<typename T>
concept HasCanAdd = requires(const Registry& r, entt::entity e) {
    { T::CanAdd(r, e) } -> std::same_as<bool>;
};

template<typename C>
concept HasOnEditPreview = requires(Registry& r, entt::entity e) { C::OnEditPreview(r, e); };

/** Also runs after undo/redo, which never call OnEditPreview, so it must cover everything OnEditPreview does. */
template<typename C>
concept HasOnEditCommit = requires(Registry& r, entt::entity e) { C::OnEditCommit(r, e); };
}

#endif //WILL_ENGINE_COMPONENT_TRAITS_H
