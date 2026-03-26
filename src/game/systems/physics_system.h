//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_PHYSICS_SYSTEM_H
#define WILL_ENGINE_PHYSICS_SYSTEM_H

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "../components/physics/physics_components.h"
#include "game/components/physics/physics_body_desc.h"
#include "Jolt/Physics/Body/BodyInterface.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/ConvexHullShape.h"
#include "Jolt/Physics/Collision/Shape/MeshShape.h"

namespace Core
{
struct FrameBuffer;
struct ViewFamily;
}

namespace Engine
{
struct GameState;
class AssetManager;
}

namespace Core
{
struct EngineContext;
}

namespace Game
{
void ConnectPhysicsObservers(entt::registry& registry);
void PhysicsUpdate(Core::EngineContext* ctx, Engine::GameState* state);
void MarkPhysicsTransformsDirty(Engine::GameState* state);
void DebugRenderPhysics(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer);
void ResolvePhysicsMeshLoads(Core::EngineContext* ctx, Engine::GameState* state);
void ResolvePhysicsShapeCreation(Core::EngineContext* ctx, Engine::GameState* state);
void ResolvePhysicsBodyCreation(Core::EngineContext* ctx, Engine::GameState* state);
void PhysicsOnPlayStop(Core::EngineContext* ctx, Engine::GameState* state);

JPH::BodyID CreateBodyFromShape(JPH::BodyInterface& bodyInterface, const Component::PhysicsBodyDesc& desc, JPH::RVec3 position, JPH::Quat rotation, JPH::ObjectLayer layerOverride = JPH::ObjectLayer(0xFFFF));
JPH::ShapeRefC CreateShapeFromDesc(const Component::PhysicsShapeDesc& desc, Engine::AssetManager* assetManager);
} // Game

#endif //WILL_ENGINE_PHYSICS_SYSTEM_H