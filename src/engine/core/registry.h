//
// Created by William on 2026-10-27.
//

#ifndef WILL_ENGINE_REGISTRY_H
#define WILL_ENGINE_REGISTRY_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>

#include <entt/entt.hpp>

#include "core/containers/inline_vector.h"
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

using PoolCreator = void(*)(entt::registry&);

inline constexpr uint32_t MAX_MODULE_POOLS = 1024;

inline Core::InlineVector<PoolCreator, MAX_MODULE_POOLS>& ModulePoolCreators()
{
    static Core::InlineVector<PoolCreator, MAX_MODULE_POOLS> creators;
    return creators;
}

template<typename T>
void CreatePool(entt::registry& raw) { (void)raw.storage<T>(); }

template<typename T>
struct PoolRegistrar
{
    inline static const bool bRegistered = [] {
        Core::InlineVector<PoolCreator, MAX_MODULE_POOLS>& creators = ModulePoolCreators();
        if (creators.Size() >= MAX_MODULE_POOLS) { std::abort(); }
        creators.PushBack(&CreatePool<T>);
        return true;
    }();
};

template<typename... T>
void RegisterPools() { ((void)PoolRegistrar<std::remove_cv_t<T>>::bRegistered, ...); }
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

    /**
     * Creates the pool of every component this DLL's code can touch. entt inserts a missing pool into a map that every lookup reads, so all of them must exist before parallel waves run. Call from each DLL, after startup and after Reset.
     */
    void CreateModulePools()
    {
        for (const RegistryDetail::PoolCreator create : RegistryDetail::ModulePoolCreators()) { create(raw); }
    }

    /** Fn: f(std::string_view typeName), in pool creation order. */
    template<typename Fn>
    void ForEachPool(Fn&& f) const
    {
        for (auto [id, pool] : std::as_const(raw).storage()) { f(std::string_view{pool.type().name()}); }
    }

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
    [[nodiscard]] const T& Get(entt::entity entity) const
    {
        RegistryDetail::RegisterPools<T>();
        return raw.get<T>(entity);
    }

    template<typename T>
    [[nodiscard]] const T* TryGet(entt::entity entity) const
    {
        RegistryDetail::RegisterPools<T>();
        return raw.try_get<T>(entity);
    }

    template<typename T>
    [[nodiscard]] T& GetMut(entt::entity entity)
    {
        RegistryDetail::RegisterPools<T>();
        return raw.get<T>(entity);
    }

    template<typename T>
    [[nodiscard]] T* TryGetMut(entt::entity entity)
    {
        RegistryDetail::RegisterPools<T>();
        return raw.try_get<T>(entity);
    }

    template<typename... T>
    [[nodiscard]] bool Has(entt::entity entity) const
    {
        RegistryDetail::RegisterPools<T...>();
        return raw.all_of<T...>(entity);
    }

    template<typename... T>
    [[nodiscard]] bool HasAny(entt::entity entity) const
    {
        RegistryDetail::RegisterPools<T...>();
        return raw.any_of<T...>(entity);
    }

    template<typename... T, typename... Exclude>
    [[nodiscard]] auto View(entt::exclude_t<Exclude...> exclude = entt::exclude_t{})
    {
        RegistryDetail::RegisterPools<typename RegistryDetail::ViewElement<T>::Type..., Exclude...>();
        return raw.view<typename RegistryDetail::ViewElement<T>::Type...>(exclude);
    }

    template<typename... T, typename... Exclude>
    [[nodiscard]] auto View(entt::exclude_t<Exclude...> exclude = entt::exclude_t{}) const
    {
        static_assert((!RegistryDetail::IsMut<T>::value && ...), "Mut<T> needs a non-const Registry");
        RegistryDetail::RegisterPools<typename RegistryDetail::ViewElement<T>::Type..., Exclude...>();
        return raw.view<typename RegistryDetail::ViewElement<T>::Type...>(exclude);
    }

    template<typename T, typename... Args>
    decltype(auto) Add(entt::entity entity, Args&&... args)
    {
        RegistryDetail::RegisterPools<T>();
        return raw.emplace<T>(entity, std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    void Set(entt::entity entity, Args&&... args)
    {
        RegistryDetail::RegisterPools<T>();
        raw.emplace_or_replace<T>(entity, std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    decltype(auto) GetOrAdd(entt::entity entity, Args&&... args)
    {
        RegistryDetail::RegisterPools<T>();
        return raw.get_or_emplace<T>(entity, std::forward<Args>(args)...);
    }

    template<typename T, typename... Other>
    void Remove(entt::entity entity)
    {
        RegistryDetail::RegisterPools<T, Other...>();
        raw.remove<T, Other...>(entity);
    }

    template<typename T, typename... Other>
    void Clear()
    {
        RegistryDetail::RegisterPools<T, Other...>();
        raw.clear<T, Other...>();
    }

    template<typename T, typename... Args>
    void Sort(Args&&... args)
    {
        RegistryDetail::RegisterPools<T>();
        raw.sort<T>(std::forward<Args>(args)...);
    }

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
