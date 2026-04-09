//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_PHYSICS_SYSTEM_H
#define WILL_ENGINE_PHYSICS_SYSTEM_H

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "game/components/physics/physics_components.h"
#include "game/components/physics/physics_body_desc.h"
#include "Jolt/Physics/Body/BodyInterface.h"


namespace Core
{
struct FrameBuffer;
struct ViewFamily;
}

namespace Engine
{
struct EngineState;
class AssetManager;
}

namespace Core
{
struct EngineContext;
}

namespace Game
{
void ConnectPhysicsObservers(entt::registry& registry);
void PhysicsUpdate(Core::EngineContext* ctx, Engine::EngineState* state);
void ResolveCollisionEvents(Core::EngineContext* ctx, Engine::EngineState* state);
void MarkPhysicsTransformsDirty(Engine::EngineState* state);
void DebugRenderPhysics(Core::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
void ResolvePhysicsMeshLoads(Core::EngineContext* ctx, Engine::EngineState* state);
void ResolvePhysicsShapeCreation(Core::EngineContext* ctx, Engine::EngineState* state);
void ResolvePhysicsBodyCreation(Core::EngineContext* ctx, Engine::EngineState* state);
void PhysicsOnPlayStop(Core::EngineContext* ctx, Engine::EngineState* state);

JPH::BodyID CreateBodyFromShape(JPH::BodyInterface& bodyInterface, const Component::PhysicsBodyDesc& desc, JPH::RVec3 position, JPH::Quat rotation, JPH::ObjectLayer layerOverride = JPH::ObjectLayer(0xFFFF));
JPH::ShapeRefC CreateShapeFromDesc(const Component::PhysicsShapeDesc& desc, Engine::AssetManager* assetManager);
} // Game

#endif //WILL_ENGINE_PHYSICS_SYSTEM_H