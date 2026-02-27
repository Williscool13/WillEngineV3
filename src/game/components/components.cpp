//
// Created by William on 2026-02-26.
//

#include "components.h"

#include "camera_components.h"
#include "core_components.h"
#include "physics_components.h"
#include "render_components.h"

namespace Game::Component
{
void StableIdComponent::Serialize(const StableIdComponent& comp, nlohmann::json& json)
{
    json["id"] = comp.id.id;
}

void StableIdComponent::Deserialize(StableIdComponent& comp, const nlohmann::json& json)
{
    comp.id = StringID(json["id"].get<uint64_t>());
}

void RegisterComponents(Core::ComponentRegistry& componentRegistry)
{
    componentRegistry.registry.Clear();

    Core::RegisterComponent<TransformComponent>(componentRegistry, "TransformComponent"_sid);
    Core::RegisterComponent<FreeCameraComponent>(componentRegistry, "FreeCameraComponent"_sid);
    Core::RegisterComponent<StaticMeshComponent>(componentRegistry, "StaticMeshComponent"_sid);
    Core::RegisterComponent<StableIdComponent>(componentRegistry, "StableIdComponent"_sid);

    Core::RegisterComponent<PhysicsBodyDesc>(componentRegistry, "PhysicsBodyDesc"_sid);
}
} // Game::Components
