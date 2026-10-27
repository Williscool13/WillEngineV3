//
// Created by William on 2026-10-27.
//

#ifndef WILL_ENGINE_REGISTRY_H
#define WILL_ENGINE_REGISTRY_H

#include <cstddef>
#include <type_traits>
#include <utility>

#include <entt/entt.hpp>

#include "engine/core/deferred_commands.h"

namespace Engine
{
struct EngineState;
struct EngineContext;
class SystemRegistry;

template<typename T>
struct Mut
{};

namespace RegistryDetail
{
template<typename T>
struct ViewElement
{
    using Type = const T;
};

template<typename T>
struct ViewElement<Mut<T>>
{
    using Type = T;
};

template<>
struct ViewElement<entt::entity>
{
    using Type = entt::entity;
};

template<typename T>
struct IsMut : std::false_type
{};

template<typename T>
struct IsMut<Mut<T>> : std::true_type
{};
}

using RegistryHook = void(*)(SystemRegistry&, entt::entity);

/** Component storage. Everything reads and writes it through a SystemRegistry; only lifecycle and hook wiring are public. */
class Registry
{
public:
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    void Bind(EngineState* state_, EngineContext* context_)
    {
        state = state_;
        context = context_;
    }

    [[nodiscard]] EngineState* State() const { return state; }
    [[nodiscard]] EngineContext* Context() const { return context; }

    void Reset() { raw = entt::registry{}; }

    template<typename T, RegistryHook Fn>
    void ConnectConstruct() { raw.on_construct<T>().template connect<&HookThunk<Fn>>(*this); }

    template<typename T, RegistryHook Fn>
    void DisconnectConstruct() { raw.on_construct<T>().template disconnect<&HookThunk<Fn>>(*this); }

    template<typename T, RegistryHook Fn>
    void ConnectUpdate() { raw.on_update<T>().template connect<&HookThunk<Fn>>(*this); }

    template<typename T, RegistryHook Fn>
    void DisconnectUpdate() { raw.on_update<T>().template disconnect<&HookThunk<Fn>>(*this); }

    template<typename T, RegistryHook Fn>
    void ConnectDestroy() { raw.on_destroy<T>().template connect<&HookThunk<Fn>>(*this); }

    template<typename T, RegistryHook Fn>
    void DisconnectDestroy() { raw.on_destroy<T>().template disconnect<&HookThunk<Fn>>(*this); }

private:
    friend class SystemRegistry;
    friend class DeferredCommands;

    /** Defined in system_registry.h. */
    template<RegistryHook Fn>
    static void HookThunk(Registry& self, entt::registry&, entt::entity entity);

    [[nodiscard]] entt::entity Create() { return raw.create(); }

    void Destroy(entt::entity entity) { raw.destroy(entity); }

    [[nodiscard]] bool Valid(entt::entity entity) const { return raw.valid(entity); }

    void ClearAll() { raw.clear(); }

    template<typename T>
    [[nodiscard]] const T& Get(entt::entity entity) const { return raw.get<T>(entity); }

    template<typename T>
    [[nodiscard]] const T* TryGet(entt::entity entity) const { return raw.try_get<T>(entity); }

    template<typename T>
    [[nodiscard]] T& GetMut(entt::entity entity) { return raw.get<T>(entity); }

    template<typename T>
    [[nodiscard]] T* TryGetMut(entt::entity entity) { return raw.try_get<T>(entity); }

    template<typename... T>
    [[nodiscard]] bool Has(entt::entity entity) const { return raw.all_of<T...>(entity); }

    template<typename... T>
    [[nodiscard]] bool HasAny(entt::entity entity) const { return raw.any_of<T...>(entity); }

    template<typename... T, typename... Exclude>
    [[nodiscard]] auto View(entt::exclude_t<Exclude...> exclude = entt::exclude_t{})
    {
        return raw.view<typename RegistryDetail::ViewElement<T>::Type...>(exclude);
    }

    template<typename... T, typename... Exclude>
    [[nodiscard]] auto View(entt::exclude_t<Exclude...> exclude = entt::exclude_t{}) const
    {
        static_assert((!RegistryDetail::IsMut<T>::value && ...), "Mut<T> needs a non-const Registry");
        return raw.view<typename RegistryDetail::ViewElement<T>::Type...>(exclude);
    }

    template<typename T, typename... Args>
    decltype(auto) Add(entt::entity entity, Args&&... args) { return raw.emplace<T>(entity, std::forward<Args>(args)...); }

    template<typename T, typename... Args>
    void Set(entt::entity entity, Args&&... args) { raw.emplace_or_replace<T>(entity, std::forward<Args>(args)...); }

    template<typename T, typename... Args>
    decltype(auto) GetOrAdd(entt::entity entity, Args&&... args) { return raw.get_or_emplace<T>(entity, std::forward<Args>(args)...); }

    template<typename T, typename... Other>
    void Remove(entt::entity entity) { raw.remove<T, Other...>(entity); }

    template<typename T, typename... Other>
    void Clear() { raw.clear<T, Other...>(); }

    template<typename T, typename... Args>
    void Sort(Args&&... args) { raw.sort<T>(std::forward<Args>(args)...); }

    entt::registry raw;
    EngineState* state{nullptr};
    EngineContext* context{nullptr};
};

template<typename T>
void DeferredCommands::ApplySet(Registry& registry, entt::entity entity, void* payload)
{
    if constexpr (std::is_empty_v<T>) {
        if (registry.Valid(entity)) { registry.Set<T>(entity); }
    }
    else {
        T& value = *static_cast<T*>(payload);
        if (registry.Valid(entity)) { registry.Set<T>(entity, std::move(value)); }
        value.~T();
    }
}

template<typename... T>
void DeferredCommands::ApplyRemove(Registry& registry, entt::entity entity, void*)
{
    if (registry.Valid(entity)) { registry.Remove<T...>(entity); }
}
} // Engine

#include "engine/core/system_registry.h"

#endif //WILL_ENGINE_REGISTRY_H
