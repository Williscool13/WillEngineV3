//
// Created by William on 2026-09-05.
//

#include "command_queue.h"

#include <tracy/Tracy.hpp>

#include "core/memory/range_allocator.h"

#include "engine/engine_api.h"
#include "engine/asset_manager.h"
#include "engine/include/engine_context.h"
#include "engine/components/physics/physics_body_component.h"
#include "engine/components/physics/physics_body_desc.h"
#include "engine/components/physics/physics_components.h"
#include "engine/components/render/light_components.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include "engine/systems/physics_integration.h"
#include "engine/components/render/reflection_probe_component.h"
#include "engine/components/render/text_component.h"
#include "physics/jolt_world.h"
#include "physics/physics_world.h"

namespace Engine
{
void PlaybackCommands(EngineContext* ctx, EngineState* state, SystemRegistry& registry)
{
    ZoneScopedN("PlaybackCommands");

    Core::Vector<Command>& commands = state->commandQueue.commands;
    for (size_t i = 0; i < commands.Size(); ++i) {
        const Command command = commands[i];
        switch (command.type) {
            case CommandType::None: {
                break;
            }
            case CommandType::PhysicsBodyConstruct: {
                if (registry.Valid(command.entity) && registry.Has<Component::PhysicsBodyDesc>(command.entity)) {
                    Component::PhysicsBodyDesc::DeferredConstruct(registry, command.entity);
                }
                break;
            }
            case CommandType::PhysicsBodyRemove: {
                if (registry.Valid(command.entity)) {
                    registry.Remove<Component::PhysicsBodyRuntime>(command.entity);
                    registry.Remove<Component::DynamicPhysicsBodyRuntime>(command.entity);
                }
                break;
            }
            case CommandType::ColliderRelease: {
                registry.Assets().UnloadCollider(command.payload.colliderHandle);
                break;
            }
            case CommandType::BodyDestroy: {
                const JPH::BodyID bodyId{command.payload.bodyId};
                JPH::BodyInterface& bodyInterface = registry.ServiceMut<Physics::PhysicsWorld>().Jolt().GetBodyInterface();
                WakeBodiesAround(bodyInterface, bodyId);
                bodyInterface.RemoveBody(bodyId);
                bodyInterface.DestroyBody(bodyId);
                break;
            }
            case CommandType::MeshRelease: {
                const MeshReleasePayload& release = command.payload.meshRelease;
                Core::RangeAllocator::Range range{release.rangeOffset, release.rangeCount};
                Core::RangeAllocator::Range modelRange{release.modelRangeOffset, release.modelRangeCount};
                registry.ServiceMut<Engine::InstanceStore>().ReleaseAndFree(&registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), range);
                if (modelRange.IsValid()) {
                    registry.ServiceMut<Engine::ModelStore>().Free(modelRange);
                }
                if (release.modelHandle.IsValid()) {
                    registry.Assets().UnloadModel(release.modelHandle);
                }
                if (release.pendingModelHandle.IsValid()) {
                    registry.Assets().UnloadModel(release.pendingModelHandle);
                }
                break;
            }
            case CommandType::AreaLightConstruct: {
                if (registry.Valid(command.entity)) {
                    if (auto* runtime = registry.TryGetMut<Component::AreaLightRuntime>(command.entity)) {
                        if (runtime->lightSlot == AnalyticLightStore::INVALID_SLOT) {
                            runtime->lightSlot = registry.ServiceMut<Engine::AnalyticLightStore>().Allocate();
                        }
                    }
                }
                break;
            }
            case CommandType::SphereLightConstruct: {
                if (registry.Valid(command.entity)) {
                    if (auto* runtime = registry.TryGetMut<Component::SphereLightRuntime>(command.entity)) {
                        if (runtime->lightSlot == AnalyticLightStore::INVALID_SLOT) {
                            runtime->lightSlot = registry.ServiceMut<Engine::AnalyticLightStore>().Allocate();
                        }
                    }
                }
                break;
            }
            case CommandType::LightSlotFree: {
                uint32_t slot = command.payload.lightSlot;
                if (slot != AnalyticLightStore::INVALID_SLOT) {
                    registry.ServiceMut<Engine::AnalyticLightStore>().Free(slot);
                }
                break;
            }
            case CommandType::CubemapRelease: {
                registry.Assets().UnloadCubemap(command.payload.cubemapHandle);
                break;
            }
            case CommandType::FontRelease: {
                registry.Assets().UnloadFont(command.payload.fontHandle);
                break;
            }
            case CommandType::ProbeConstruct: {
                if (registry.Valid(command.entity) && registry.Has<Component::ReflectionProbeComponent>(command.entity)) {
                    Component::ReflectionProbeComponent::DeferredConstruct(registry, command.entity);
                }
                break;
            }
            case CommandType::TextConstruct: {
                if (registry.Valid(command.entity)) {
                    if (auto* text = registry.TryGetMut<Component::TextComponent>(command.entity)) {
                        Component::LoadTextComponent(*text, registry, command.entity);
                    }
                }
                break;
            }
            case CommandType::DestroyEntity: {
                if (registry.Valid(command.entity)) {
                    registry.Destroy(command.entity);
                }
                break;
            }
        }
    }
    commands.Clear();
}
} // Engine
