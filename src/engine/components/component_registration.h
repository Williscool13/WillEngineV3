//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMPONENT_REGISTRY_H
#define WILL_ENGINE_COMPONENT_REGISTRY_H

#include "component_editor.h"
#include "core/string_id.h"
#include "engine/core/registry.h"
#include "engine/component_registry.h"
#include "engine/components/component_types.h"
#include "engine/reflection/reflection_entity_refs.h"
#include "engine/reflection/reflection_serialize.h"
#include "engine/editor/reflected_inspector.h"


namespace Engine
{
template<DataComponent T> requires NamedComponent<T>
void RegisterComponent(Engine::ComponentRegistry& componentRegistry, Origin origin, bool hidden, bool hideInInspector)
{
    static_assert(ReflectedSerializable<T>, "data components must be reflected, with FieldTraits for every field type");
    auto typeId = TypeSID<T>();
    auto index = componentRegistry.registry.Size();
    assert(componentRegistry.registryMapping.Find(typeId) == nullptr && "COMPONENT_NAME collision");
    componentRegistry.registry.PushBack({
        typeId,
        ComponentName<T>(),
        [](const Engine::SystemRegistry& reg, entt::entity e, Engine::TextWriter& w) {
            SerializeFields(reg.Get<T>(e), w);
        },
        [](Engine::SystemRegistry& reg, entt::entity e, const Engine::TextReader& r) {
            T comp{};
            DeserializeFields(comp, r);
            reg.Set<T>(e, std::move(comp));
        },
        [](const Engine::SystemRegistry& reg, entt::entity e) -> bool {
            if constexpr (HasCanAdd<T>) {
                return T::CanAdd(reg, e);
            }
            else {
                return true;
            }
        },
        [](Engine::SystemRegistry& reg, entt::entity e) {
            T a = reg.GetOrAdd<T>(e);
        },
        [](Engine::SystemRegistry& reg, entt::entity e) {
            reg.Remove<T>(e);
        },
        [](const Engine::SystemRegistry& srcReg, entt::entity srcEntity, Engine::SystemRegistry& dstReg, entt::entity dstEntity) {
            dstReg.Set<T>(dstEntity, srcReg.Get<T>(srcEntity));
        },
        [](Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* n) {
            if constexpr (HasDrawEditor<T>) {
                return T::DrawEditor(viewFamily, edit, n);
            }
            else {
                return DrawReflectedComponentEditor<T>(edit, n);
            }
        },
        [](const Engine::SystemRegistry& reg, entt::entity e) -> bool {
            return reg.Has<T>(e);
        },
        origin,
        hidden,
        hideInInspector
    });

    componentRegistry.registry[index].restore = [](Engine::SystemRegistry& reg, entt::entity e, const Engine::TextReader& r) {
        T fresh{};
        DeserializeFields(fresh, r);
        T& live = reg.GetMut<T>(e);
        ForEachField<T>([&live, &fresh](const auto& f) { AssignValue(live.*f.member, fresh.*f.member); });
        if constexpr (HasOnEditCommit<T>) {
            T::OnEditCommit(reg, e);
        }
    };

    componentRegistry.registry[index].fillDefaults = [](const Engine::TextReader& r, Engine::TextWriter& w) {
        T comp{};
        DeserializeFields(comp, r);
        SerializeFields(comp, w);
    };

    componentRegistry.registry[index].propagate = [](Engine::SystemRegistry& reg, entt::entity e, const Engine::TextReader& before, const Engine::TextReader& after) -> bool {
        T b{};
        T a{};
        DeserializeFields(b, before);
        DeserializeFields(a, after);
        if (DeepEqual(b, a)) { return false; }
        T& live = reg.GetMut<T>(e);
        T updated = live;
        PropagateDiff(updated, b, a);
        if (DeepEqual(updated, live)) { return false; }
        ForEachField<T>([&live, &updated](const auto& f) { AssignValue(live.*f.member, updated.*f.member); });
        if constexpr (HasOnEditCommit<T>) {
            T::OnEditCommit(reg, e);
        }
        return true;
    };

    if constexpr (ContainsEntityRef<T>()) {
        componentRegistry.registry[index].remapEntityRefs = [](Engine::SystemRegistry& reg, entt::entity e, Core::Span<const StringID> from, Core::Span<const StringID> to) {
            ForEachEntityRef(reg.GetMut<T>(e), [&from, &to](EntityRef& ref) {
                for (size_t i = 0; i < from.Size(); ++i) {
                    if (ref.stableId == from[i]) {
                        ref = EntityRef{to[i]};
                        return;
                    }
                }
            });
        };
    }

    componentRegistry.registry[index].bMovesEntity = MovesEntity<T>;
    componentRegistry.registry[index].bInstanceOnly = InstanceOnly<T>;
    componentRegistry.registryMapping[typeId] = index;
}

template<TagComponent T> requires NamedComponent<T>
void RegisterComponent(Engine::ComponentRegistry& componentRegistry, Origin origin, bool hidden, bool hideInInspector)
{
    auto typeId = TypeSID<T>();
    auto index = componentRegistry.registry.Size();
    assert(componentRegistry.registryMapping.Find(typeId) == nullptr && "COMPONENT_NAME collision");
    componentRegistry.registry.PushBack({
        typeId,
        ComponentName<T>(),
        [](const Engine::SystemRegistry&, entt::entity, Engine::TextWriter&) {},
        [](Engine::SystemRegistry& reg, entt::entity e, const Engine::TextReader&) {
            (void) reg.GetOrAdd<T>(e);
        },
        [](const Engine::SystemRegistry& reg, entt::entity e) -> bool {
            if constexpr (HasCanAdd<T>) {
                return T::CanAdd(reg, e);
            }
            else {
                return true;
            }
        },
        [](Engine::SystemRegistry& reg, entt::entity e) {
            reg.GetOrAdd<T>(e);
        },
        [](Engine::SystemRegistry& reg, entt::entity e) {
            reg.Remove<T>(e);
        },
        [](const Engine::SystemRegistry&, entt::entity, Engine::SystemRegistry& dstReg, entt::entity dstEntity) {
            (void) dstReg.GetOrAdd<T>(dstEntity);
        },
        [](Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* n) {
            if constexpr (HasDrawEditor<T>) {
                return T::DrawEditor(viewFamily, edit, n);
            }
            else {
                return DefaultDrawComponentEditor(n);
            }
        },
        [](const Engine::SystemRegistry& reg, entt::entity e) -> bool {
            return reg.Has<T>(e);
        },
        origin,
        hidden,
        hideInInspector
    });
    componentRegistry.registry[index].bMovesEntity = MovesEntity<T>;
    componentRegistry.registryMapping[typeId] = index;
}

/** Saves the reflected fields (leave cache fields unreflected), or presence for a fieldless type. */
template<NamedComponent T>
void RegisterRuntimeState(Engine::ComponentRegistry& componentRegistry, Origin origin)
{
    auto typeId = TypeSID<T>();
    assert(componentRegistry.runtimeStateMapping.Find(typeId) == nullptr && componentRegistry.registryMapping.Find(typeId) == nullptr && "COMPONENT_NAME collision");
    RuntimeStateEntry entry{
        .typeId = typeId,
        .name = ComponentName<T>(),
        .has = [](const Engine::SystemRegistry& reg, entt::entity e) -> bool { return reg.Has<T>(e); },
        .origin = origin,
    };
    if constexpr (TagComponent<T>) {
        entry.serialize = [](const Engine::SystemRegistry&, entt::entity, Engine::TextWriter&) {};
        entry.deserialize = [](Engine::SystemRegistry& reg, entt::entity e, const Engine::TextReader&) { (void) reg.GetOrAdd<T>(e); };
    }
    else {
        static_assert(ReflectedSerializable<T>, "runtime state must reflect its state fields, with FieldTraits for every field type");
        entry.serialize = [](const Engine::SystemRegistry& reg, entt::entity e, Engine::TextWriter& w) { SerializeFields(reg.Get<T>(e), w); };
        entry.deserialize = [](Engine::SystemRegistry& reg, entt::entity e, const Engine::TextReader& r) { DeserializeFields(reg.GetOrAdd<T>(e), r); };
    }
    componentRegistry.runtimeStateMapping[typeId] = componentRegistry.runtimeState.Size();
    componentRegistry.runtimeState.PushBack(entry);
}

void RegisterEngineComponents(Engine::ComponentRegistry& componentRegistry);
} // Engine

#endif //WILL_ENGINE_COMPONENT_REGISTRY_H
