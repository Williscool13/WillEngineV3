//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMPONENT_REGISTRY_H
#define WILL_ENGINE_COMPONENT_REGISTRY_H
#include <entt/entt.hpp>

#include "component_editor.h"
#include "core/string_id.h"
#include "engine/component_registry.h"
#include "game/components/component_types.h"


namespace Game
{
template<typename T>
concept TagComponent = std::is_empty_v<T>;

template<typename T>
concept DataComponent = !std::is_empty_v<T>;

template<DataComponent T> requires NamedComponent<T>
void RegisterComponent(Engine::ComponentRegistry& componentRegistry, bool hidden, bool hideInInspector)
{
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
        },
        [](entt::registry& reg, entt::entity e, const Engine::TextReader& r) {
            T comp{};
            if constexpr (HasDeserialize<T>) {
                T::Deserialize(comp, r);
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
        [](Core::ViewFamily& viewFamily, entt::registry& reg, entt::entity e, const char* n) {
            if constexpr (HasDrawEditor<T>) {
                return T::DrawEditor(viewFamily, reg, e, n);
            }
            else {
                return DefaultDrawComponentEditor(n);
            }
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            return reg.all_of<T>(e);
        },
        hidden,
        hideInInspector
    });

    componentRegistry.registryMapping[typeId] = index;
}

template<TagComponent T> requires NamedComponent<T>
void RegisterComponent(Engine::ComponentRegistry& componentRegistry, bool hidden, bool hideInInspector)
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
        [](Core::ViewFamily& viewFamily, entt::registry& reg, entt::entity e, const char* n) {
            if constexpr (HasDrawEditor<T>) {
                return T::DrawEditor(viewFamily, reg, e, n);
            }
            else {
                return DefaultDrawComponentEditor(n);
            }
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            return reg.all_of<T>(e);
        },
        hidden,
        hideInInspector
    });
    componentRegistry.registryMapping[typeId] = index;
}

void RegisterComponents(Engine::ComponentRegistry& componentRegistry);
} // Core

#endif //WILL_ENGINE_COMPONENT_REGISTRY_H
