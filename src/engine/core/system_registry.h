//
// Created by William on 2026-10-28.
//

#ifndef WILL_ENGINE_SYSTEM_REGISTRY_H
#define WILL_ENGINE_SYSTEM_REGISTRY_H

#include <cassert>
#include <type_traits>
#include <utility>

#include <entt/entt.hpp>

#include "engine/components/component_identity.h"
#include "engine/core/access_check.h"
#include "engine/core/access_set.h"
#include "engine/core/deferred_commands.h"
#include "engine/core/registry.h"

namespace Engine
{
struct Command;

/**
 * Exclusive access applies immediately and is unchecked. Non-exclusive access defers Set/Remove/Destroy/PushCommand to the end of the wave
 * and checks every touch against the declaration; Add/GetOrAdd/Create/Clear/Sort/ClearAll are exclusive only.
 */
class SystemRegistry
{
public:
    SystemRegistry(Registry& registry_, const AccessSet& access_, DeferredCommands* commands_, const char* name_)
        : registry(registry_), access(access_), commands(commands_), name(name_)
    {
        assert(access.bExclusive == (commands == nullptr) && "Exclusive access applies immediately; non-exclusive access needs a command buffer");
    }

    SystemRegistry(const SystemRegistry&) = delete;
    SystemRegistry& operator=(const SystemRegistry&) = delete;

    [[nodiscard]] EngineState* State() const { return registry.State(); }
    [[nodiscard]] EngineContext* Context() const { return registry.Context(); }
    [[nodiscard]] const char* Name() const { return name; }

    [[nodiscard]] entt::entity Create()
    {
        CheckStructural("Create");
        return registry.Create();
    }

    void Destroy(entt::entity entity)
    {
        if (commands != nullptr) {
            commands->Destroy(entity);
            return;
        }
        registry.Destroy(entity);
    }

    [[nodiscard]] bool Valid(entt::entity entity) const { return registry.Valid(entity); }

    void ClearAll()
    {
        CheckStructural("ClearAll");
        registry.ClearAll();
    }

    template<typename T>
    [[nodiscard]] const T& Get(entt::entity entity) const
    {
        CheckRead<T>();
        return registry.Get<T>(entity);
    }

    template<typename T>
    [[nodiscard]] const T* TryGet(entt::entity entity) const
    {
        CheckRead<T>();
        return registry.TryGet<T>(entity);
    }

    template<typename T>
    [[nodiscard]] T& GetMut(entt::entity entity)
    {
        CheckWrite<T>();
        return registry.GetMut<T>(entity);
    }

    template<typename T>
    [[nodiscard]] T* TryGetMut(entt::entity entity)
    {
        CheckWrite<T>();
        return registry.TryGetMut<T>(entity);
    }

    template<typename... T>
    [[nodiscard]] bool Has(entt::entity entity) const { return registry.Has<T...>(entity); }

    template<typename... T>
    [[nodiscard]] bool HasAny(entt::entity entity) const { return registry.HasAny<T...>(entity); }

    template<typename... T, typename... Exclude>
    [[nodiscard]] auto View(entt::exclude_t<Exclude...> exclude = entt::exclude_t{})
    {
        (CheckViewElement<T>(), ...);
        return registry.View<T...>(exclude);
    }

    template<typename... T, typename... Exclude>
    [[nodiscard]] auto View(entt::exclude_t<Exclude...> exclude = entt::exclude_t{}) const
    {
        (CheckViewElement<T>(), ...);
        return std::as_const(registry).template View<T...>(exclude);
    }

    template<typename T, typename... Args>
    decltype(auto) Add(entt::entity entity, Args&&... args)
    {
        CheckStructural("Add");
        return registry.Add<T>(entity, std::forward<Args>(args)...);
    }

    /** Under non-exclusive access the caller does not see the result until the next wave. */
    template<typename T, typename... Args>
    void Set(entt::entity entity, Args&&... args)
    {
        if (commands != nullptr) {
            CheckWrite<T>();
            commands->Set<T>(entity, std::forward<Args>(args)...);
            return;
        }
        registry.Set<T>(entity, std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    decltype(auto) GetOrAdd(entt::entity entity, Args&&... args)
    {
        CheckStructural("GetOrAdd");
        return registry.GetOrAdd<T>(entity, std::forward<Args>(args)...);
    }

    template<typename T, typename... Other>
    void Remove(entt::entity entity)
    {
        if (commands != nullptr) {
            CheckWrite<T>();
            (CheckWrite<Other>(), ...);
            commands->Remove<T, Other...>(entity);
            return;
        }
        registry.Remove<T, Other...>(entity);
    }

    template<typename T, typename... Other>
    void Clear()
    {
        CheckStructural("Clear");
        registry.Clear<T, Other...>();
    }

    template<typename T, typename... Args>
    void Sort(Args&&... args)
    {
        CheckStructural("Sort");
        registry.Sort<T>(std::forward<Args>(args)...);
    }

    void PushCommand(const Command& command);

private:
    template<typename T>
    void CheckRead() const
    {
#if WILL_ACCESS_CHECK
        if (access.bExclusive) { return; }
        if constexpr (NamedComponent<T>) { CheckReadAccess(name, access, AccessId<T>()); }
        else if constexpr (!std::is_same_v<T, entt::entity>) { CheckUnnamedAccess(name, entt::type_name<T>::value()); }
#endif
    }

    template<typename T>
    void CheckWrite() const
    {
#if WILL_ACCESS_CHECK
        if (access.bExclusive) { return; }
        if constexpr (NamedComponent<T>) { CheckWriteAccess(name, access, AccessId<T>()); }
        else { CheckUnnamedAccess(name, entt::type_name<T>::value()); }
#endif
    }

    template<typename T>
    void CheckViewElement() const
    {
        if constexpr (RegistryDetail::IsMut<T>::value) { CheckWrite<typename RegistryDetail::ViewElement<T>::Type>(); }
        else { CheckRead<T>(); }
    }

    void CheckStructural([[maybe_unused]] const char* operation) const
    {
#if WILL_ACCESS_CHECK
        if (!access.bExclusive) { CheckStructuralAccess(name, operation); }
#endif
    }

    Registry& registry;
    const AccessSet& access;
    DeferredCommands* commands;
    const char* name;
};

template<RegistryHook Fn>
void Registry::HookThunk(Registry& self, entt::registry&, entt::entity entity)
{
    SystemRegistry registry{self, AccessSet::EXCLUSIVE, nullptr, "Hook"};
    Fn(registry, entity);
}
} // Engine

#endif //WILL_ENGINE_SYSTEM_REGISTRY_H
