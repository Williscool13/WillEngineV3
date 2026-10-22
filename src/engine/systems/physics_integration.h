//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_PHYSICS_INTEGRATION_H
#define WILL_ENGINE_PHYSICS_INTEGRATION_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "engine/asset_manager_types.h"
#include "engine/components/physics/physics_body_desc.h"

namespace Engine
{
class AssetManager;

/** Activates every body overlapping this body's bounds; moving or removing a body does not wake what rests on it by itself. */
void WakeBodiesAround(JPH::BodyInterface& bodyInterface, const JPH::BodyID& bodyId);

JPH::BodyID CreateBodyFromShape(JPH::BodyInterface& bodyInterface, const Component::PhysicsBodyDesc& desc, const JPH::ShapeRefC& shapeRef, JPH::RVec3 position, JPH::Quat rotation,
                                uint64_t userData, uint16_t layerOverride = Component::NO_LAYER_OVERRIDE);
JPH::ShapeRefC CreateShapeFromDesc(const Component::PhysicsShapeDesc& desc, Engine::PhysicsColliderHandle colliderHandle, Engine::AssetManager* assetManager);
} // Engine

#endif //WILL_ENGINE_PHYSICS_INTEGRATION_H
