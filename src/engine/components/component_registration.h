//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMPONENT_REGISTRY_H
#define WILL_ENGINE_COMPONENT_REGISTRY_H
#include <entt/entt.hpp>

#include "component_editor.h"
#include "core/string_id.h"
#include "engine/component_registry.h"
#include "engine/components/component_types.h"
#include "engine/reflection/reflection_serialize.h"
#include "engine/editor/reflected_inspector.h"


namespace Engine
{
template<typename T>
concept TagComponent = std::is_empty_v<T>;

template<typename T>
concept DataComponent = !std::is_empty_v<T>;

template<DataComponent T> requires NamedComponent<T>
void RegisterComponent(Engine::ComponentRegistry& componentRegistry, Origin origin, bool hidden, bool hideInInspector)
{
    static_assert(!Reflected<T> || HasSerialize<T> || ReflectedSerializable<T>, "reflected component has a field type without FieldTraits and no hand-written Serialize");
    auto typeId = TypeSID<T>();
    auto index = componentRegistry.registry.Size();
    assert(componentRegistry.registryMapping.Find(typeId) == nullptr && "COMPONENT_NAME collision");
    componentRegistry.registry.PushBack({
        typeId,
        T::COMPONENT_NAME,
        [](const entt::registry& reg, entt::entity e, Engine::TextWriter& w) {
            if constexpr (HasSerialize<T>) {
                T::Serialize(reg.get<T>(e), w);
            }
            else if constexpr (ReflectedSerializable<T>) {
                SerializeFields(reg.get<T>(e), w);
            }
        },
        [](entt::registry& reg, entt::entity e, const Engine::TextReader& r) {
            T comp{};
            if constexpr (HasDeserialize<T>) {
                T::Deserialize(comp, r);
            }
            else if constexpr (ReflectedSerializable<T>) {
                DeserializeFields(comp, r);
            }
            reg.emplace_or_replace<T>(e, std::move(comp));
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            if constexpr (HasCanAdd<T>) {
                return T::CanAdd(reg, e);
            }
            else {
                return true;
            }
        },
        [](entt::registry& reg, entt::entity e) {
            T a = reg.get_or_emplace<T>(e);
        },
        [](entt::registry& reg, entt::entity e) {
            reg.remove<T>(e);
        },
        [](const entt::registry& srcReg, entt::entity srcEntity, entt::registry& dstReg, entt::entity dstEntity) {
            dstReg.emplace_or_replace<T>(dstEntity, srcReg.get<T>(srcEntity));
        },
        [](Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* n) {
            if constexpr (HasDrawEditor<T>) {
                return T::DrawEditor(viewFamily, edit, n);
            }
            else if constexpr (Reflected<T>) {
                return DrawReflectedComponentEditor<T>(edit, n);
            }
            else {
                return DefaultDrawComponentEditor(n);
            }
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            return reg.all_of<T>(e);
        },
        origin,
        hidden,
        hideInInspector
    });

    componentRegistry.registry[index].restore = [](entt::registry& reg, entt::entity e, const Engine::TextReader& r) {
        T fresh{};
        if constexpr (HasDeserialize<T>) {
            T::Deserialize(fresh, r);
        }
        else if constexpr (ReflectedSerializable<T>) {
            DeserializeFields(fresh, r);
        }
        if constexpr (Reflected<T> && !RestoresByRebuild<T>) {
            T& live = reg.get<T>(e);
            ForEachField<T>([&live, &fresh](const auto& f) { AssignValue(live.*f.member, fresh.*f.member); });
            if constexpr (HasOnEditCommit<T>) {
                T::OnEditCommit(reg, e);
            }
        }
        else {
            reg.remove<T>(e);
            reg.emplace<T>(e, std::move(fresh));
        }
    };

    componentRegistry.registry[index].fillDefaults =[](const Engine::TextReader& r, Engine::TextWriter& w) {
        T comp{};
        if constexpr (HasDeserialize<T>) {
            T::Deserialize(comp, r);
        }
        else if constexpr (ReflectedSerializable<T>) {
            DeserializeFields(comp, r);
        }
        if constexpr (HasSerialize<T>) {
            T::Serialize(comp, w);
        }
        else if constexpr (ReflectedSerializable<T>) {
            SerializeFields(comp, w);
        }
    };

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
        T::COMPONENT_NAME,
        [](const entt::registry&, entt::entity, Engine::TextWriter&) {},
        [](entt::registry& reg, entt::entity e, const Engine::TextReader&) {
            (void) reg.get_or_emplace<T>(e);
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            if constexpr (HasCanAdd<T>) {
                return T::CanAdd(reg, e);
            }
            else {
                return true;
            }
        },
        [](entt::registry& reg, entt::entity e) {
            reg.get_or_emplace<T>(e);
        },
        [](entt::registry& reg, entt::entity e) {
            reg.remove<T>(e);
        },
        [](const entt::registry&, entt::entity, entt::registry& dstReg, entt::entity dstEntity) {
            (void) dstReg.get_or_emplace<T>(dstEntity);
        },
        [](Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* n) {
            if constexpr (HasDrawEditor<T>) {
                return T::DrawEditor(viewFamily, edit, n);
            }
            else {
                return DefaultDrawComponentEditor(n);
            }
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            return reg.all_of<T>(e);
        },
        origin,
        hidden,
        hideInInspector
    });
    componentRegistry.registryMapping[typeId] = index;
}

void RegisterEngineComponents(Engine::ComponentRegistry& componentRegistry);
} // Engine

#endif //WILL_ENGINE_COMPONENT_REGISTRY_H
