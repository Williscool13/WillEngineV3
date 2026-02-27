//
// Created by William on 2026-02-26.
//

#include "components.h"

#include "camera_components.h"
#include "core_components.h"
#include "render_components.h"

namespace Game::Components
{
void RegisterComponents(Core::ComponentRegistry& componentRegistry)
{
    componentRegistry.registry.Clear();

    Core::RegisterComponent<Component::TransformComponent>(componentRegistry, "TransformComponent"_sid);
    Core::RegisterComponent<Component::FreeCameraComponent>(componentRegistry, "FreeCameraComponent"_sid);
    Core::RegisterComponent<Component::StaticMeshComponent>(componentRegistry, "StaticMeshComponent"_sid);
}
} // Game::Components
