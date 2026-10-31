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

namespace Audio
{
class AudioManager;
}

namespace Physics
{
class PhysicsWorld;
}

namespace Core
{
struct FrameBuffer;
struct RenderView;
struct ViewFamily;
struct ViewFamilyRenderables;
struct ViewFamilyText;
struct ViewFamilySprites;
struct ViewFamilyFog;
struct ViewFamilyLights;
struct ViewFamilyBakedShadows;
struct ViewFamilyProbes;
struct ViewFamilyDDGIVolumes;
struct ViewFamilySettings;
struct ViewFamilyDebugDraws;
struct ViewFamilyUI;
}

namespace Engine
{
struct Command;
class AssetManager;
class IAssetLoader;
class MaterialManager;
class IMaterialStore;
class MaterialUploadDirty;
class MaterialChangedDirty;
class InstanceStore;
class InstanceRecords;
class InstanceWorldBounds;
class InstanceBoundsChanges;
class InstanceDirty;
class ModelStore;
class TriLightStore;
class AnalyticLightStore;
struct LocalShadowSelection;

/**
 * Exclusive access applies immediately and is unchecked. Non-exclusive access defers Set/Remove/Destroy/PushCommand to the end of the wave
 * and checks every touch against the declaration: Get needs any declaration, GetMut needs Writes, Set/Remove need Writes or DeferredWrites.
 * Add/GetOrAdd/Create/Clear/Sort/ClearAll are exclusive only, or go through RunExclusive.
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

    /** A ctx service (MaterialManager, AudioManager, PhysicsWorld); its const methods are safe to call concurrently. A service with EXCLUSIVE_ONLY is only available to exclusive systems. */
    template<typename T>
    [[nodiscard]] const T& Service() const
    {
        if constexpr (requires { T::EXCLUSIVE_ONLY; }) { CheckExclusiveOnly<T>(); }
        else { CheckRead<T>(); }
        return *ServiceSlot<T>(*Context());
    }

    template<typename T>
    [[nodiscard]] T& ServiceMut()
    {
        if constexpr (requires { T::EXCLUSIVE_ONLY; }) { CheckExclusiveOnly<T>(); }
        else { CheckWrite<T>(); }
        return *ServiceSlot<T>(*Context());
    }

    /** The frame buffer being prepared, or a part of its view family. Only available during PrepareFrame. */
    template<typename T>
    [[nodiscard]] const T& Frame() const
    {
        CheckRead<T>();
        return FrameSlot<T>(*Context());
    }

    template<typename T>
    [[nodiscard]] T& FrameMut()
    {
        CheckWrite<T>();
        return FrameSlot<T>(*Context());
    }

    template<typename Ctx = EngineContext>
    [[nodiscard]] IAssetLoader& Assets() const
    {
        return *static_cast<Ctx*>(Context())->services.assetLoader;
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
            CheckDeferredWrite<T>();
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
            CheckDeferredWrite<T>();
            (CheckDeferredWrite<Other>(), ...);
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

    /**
     * Calls Fn(ctx, state, registry, const Payload&) with exclusive access: immediately under exclusive access, otherwise when the wave's commands apply, in record order.
     * For work a declaration cannot cover, such as spawning; the payload is copied, so it must not point at anything the wave frees.
     */
    template<auto Fn, typename Payload>
    void RunExclusive(Payload&& payload)
    {
        if (commands != nullptr) {
            commands->Record(&ApplyRunExclusive<Fn, std::remove_cvref_t<Payload>>, entt::null, std::forward<Payload>(payload));
            return;
        }
        Fn(Context(), State(), *this, payload);
    }

private:
    [[nodiscard]] EngineState* State() const { return registry.State(); }
    [[nodiscard]] EngineContext* Context() const { return registry.Context(); }

    static void ApplyPushCommand(Registry& registry, entt::entity, void* payload);

    /** Context is a template parameter so EngineContext only has to be complete where a service is used. */
    template<typename T, typename Context>
    static T* ServiceSlot(Context& ctx)
    {
        if constexpr (std::is_same_v<T, AssetManager>) { return ctx.services.assetManager; }
        else if constexpr (std::is_same_v<T, MaterialManager>) { return ctx.services.materialManager; }
        else if constexpr (std::is_same_v<T, IMaterialStore>) { return ctx.services.materialStore; }
        else if constexpr (std::is_same_v<T, MaterialUploadDirty>) { return ctx.services.materialUploadDirty; }
        else if constexpr (std::is_same_v<T, MaterialChangedDirty>) { return ctx.services.materialChangedDirty; }
        else if constexpr (std::is_same_v<T, InstanceStore>) { return ctx.services.instanceStore; }
        else if constexpr (std::is_same_v<T, InstanceRecords>) { return ctx.services.instanceRecords; }
        else if constexpr (std::is_same_v<T, InstanceWorldBounds>) { return ctx.services.instanceWorldBounds; }
        else if constexpr (std::is_same_v<T, InstanceBoundsChanges>) { return ctx.services.instanceBoundsChanges; }
        else if constexpr (std::is_same_v<T, InstanceDirty>) { return ctx.services.instanceDirty; }
        else if constexpr (std::is_same_v<T, ModelStore>) { return ctx.services.modelStore; }
        else if constexpr (std::is_same_v<T, TriLightStore>) { return ctx.services.triLightStore; }
        else if constexpr (std::is_same_v<T, AnalyticLightStore>) { return ctx.services.analyticLightStore; }
        else if constexpr (std::is_same_v<T, LocalShadowSelection>) { return ctx.services.localShadowSelection; }
        else if constexpr (std::is_same_v<T, Audio::AudioManager>) { return ctx.services.audioManager; }
        else if constexpr (std::is_same_v<T, Physics::PhysicsWorld>) { return ctx.services.physicsWorld; }
        else { static_assert(sizeof(T) == 0, "not a service"); }
    }

    template<typename T, typename Context>
    static T& FrameSlot(Context& ctx)
    {
        assert(ctx.services.frameBuffer != nullptr && "The frame buffer is only available during PrepareFrame");
        auto& family = ctx.services.frameBuffer->mainViewFamily;
        if constexpr (std::is_same_v<T, Core::FrameBuffer>) { return *ctx.services.frameBuffer; }
        else if constexpr (std::is_same_v<T, Core::ViewFamily>) { return family; }
        else if constexpr (std::is_same_v<T, Core::RenderView>) { return family.mainView; }
        else if constexpr (std::is_same_v<T, Core::ViewFamilyRenderables>) { return family.renderables; }
        else if constexpr (std::is_same_v<T, Core::ViewFamilyText>) { return family.text; }
        else if constexpr (std::is_same_v<T, Core::ViewFamilySprites>) { return family.spriteDraws; }
        else if constexpr (std::is_same_v<T, Core::ViewFamilyFog>) { return family.fog; }
        else if constexpr (std::is_same_v<T, Core::ViewFamilyLights>) { return family.lights; }
        else if constexpr (std::is_same_v<T, Core::ViewFamilyBakedShadows>) { return family.bakedShadows; }
        else if constexpr (std::is_same_v<T, Core::ViewFamilyProbes>) { return family.probes; }
        else if constexpr (std::is_same_v<T, Core::ViewFamilyDDGIVolumes>) { return family.ddgiVolumes; }
        else if constexpr (std::is_same_v<T, Core::ViewFamilySettings>) { return family.settings; }
        else if constexpr (std::is_same_v<T, Core::ViewFamilyDebugDraws>) { return family.debugDraws; }
        else if constexpr (std::is_same_v<T, Core::ViewFamilyUI>) { return family.ui; }
        else { static_assert(sizeof(T) == 0, "not part of the frame buffer"); }
    }

    template<auto Fn, typename Payload>
    static void ApplyRunExclusive(Registry& storage, entt::entity, void* payload)
    {
        Payload& value = *static_cast<Payload*>(payload);
        SystemRegistry registry{storage, AccessSet::EXCLUSIVE, nullptr, "RunExclusive"};
        Fn(storage.Context(), storage.State(), registry, std::as_const(value));
        value.~Payload();
    }

    template<typename T>
    void CheckExclusiveOnly() const
    {
#if WILL_ACCESS_CHECK
        if (!access.bExclusive) { CheckStructuralAccess(name, T::COMPONENT_NAME); }
#endif
    }

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
    void CheckDeferredWrite() const
    {
#if WILL_ACCESS_CHECK
        if (access.bExclusive) { return; }
        if constexpr (NamedComponent<T>) { CheckDeferredWriteAccess(name, access, AccessId<T>()); }
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
    Fn(self.Context(), self.State(), registry, entity);
}
} // Engine

#endif //WILL_ENGINE_SYSTEM_REGISTRY_H
