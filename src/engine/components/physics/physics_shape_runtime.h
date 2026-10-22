//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_PHYSICS_SHAPE_RUNTIME_H
#define WILL_ENGINE_PHYSICS_SHAPE_RUNTIME_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "core/containers/inline_vector.h"
#include "engine/asset_manager_types.h"

namespace Engine::Component
{
/** Runtime state derived from PhysicsBodyDesc; colliders is index-parallel to shapes and rebuilt with them. Engine physics integration only. */
struct PhysicsShapeRuntime
{
    Core::InlineVector<Engine::PhysicsColliderHandle, 8> colliders;
    // potentially also store its type (e.g. compound)
    JPH::ShapeRefC shapeRef;
};
}

#endif //WILL_ENGINE_PHYSICS_SHAPE_RUNTIME_H
