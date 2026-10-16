//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_PHYSICS_COMPONENTS_H
#define WILL_ENGINE_PHYSICS_COMPONENTS_H

#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>
#include <entt/entt.hpp>

#include "engine/component_registry.h"
#include "engine/components/component_types.h"
#include "engine/reflection/reflection.h"

namespace Core { struct ViewFamily; }

namespace Engine::Component
{
/** Physics state. Interpolate w/ physics alpha for smooth physics*/
struct DynamicPhysicsBodyComponent
{
    glm::vec3 previousPosition{};
    glm::quat previousRotation{};
    glm::vec3 currentPosition{};
    glm::quat currentRotation{};
};

struct TeleportPhysicsTransformTag
{};

struct StaticFollowWarnedTag
{};

struct DirtyKinematicPhysicsTransformTag
{};

struct PendingPhysicsMeshTag
{};

struct PhysicsMeshLoadingTag
{};

struct PendingPhysicsShapeCreationTag
{};

struct PendingPhysicsBodyCreationTag
{};

struct SetVelocityTag
{
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
};

struct DrawPhysicsDebugTag
{
    static constexpr const char* COMPONENT_NAME = "DrawPhysicsDebugTag";
};

/** Moves whatever touches the body along its surface; the body itself stays put. Circular turns about the local axis through the origin, speed measured at referenceRadius. */
struct ConveyorComponent
{
    static constexpr const char* COMPONENT_NAME = "ConveyorComponent";

    enum class Mode : uint32_t
    {
        Linear = 0,
        Circular = 1,
    };

    Mode mode{Mode::Linear};
    glm::vec3 direction{1.0f, 0.0f, 0.0f};
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float speed{1.0f};
    float referenceRadius{1.0f};
    bool bEnabled{true};

    WILL_REFLECT(ConveyorComponent,
        WILL_FIELD(mode),
        WILL_FIELD(direction),
        WILL_FIELD(axis),
        WILL_FIELD(speed, .speed = 0.05f),
        WILL_FIELD(referenceRadius, .min = 0.01f, .speed = 0.05f),
        WILL_FIELD(bEnabled))

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};
}

#endif //WILL_ENGINE_PHYSICS_COMPONENTS_H
