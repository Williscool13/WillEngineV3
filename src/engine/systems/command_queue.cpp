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
#include "engine/components/render/reflection_probe_component.h"
#include "engine/components/render/text_component.h"
#include "physics/physics_system.h"

namespace Engine
{
void PlaybackCommands(EngineContext* ctx, EngineState* state)
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
                if (state->registry.valid(command.entity) && state->registry.all_of<Component::PhysicsBodyDesc>(command.entity)) {
                    Component::PhysicsBodyDesc::DeferredConstruct(state->registry, command.entity);
                }
                break;
            }
            case CommandType::PhysicsBodyRemove: {
                if (state->registry.valid(command.entity)) {
                    state->registry.remove<Component::PhysicsBodyComponent>(command.entity);
                    state->registry.remove<Component::DynamicPhysicsBodyComponent>(command.entity);
                }
                break;
            }
            case CommandType::ColliderRelease: {
                ctx->assetManager->UnloadCollider(command.payload.colliderHandle);
                break;
            }
            case CommandType::BodyDestroy: {
                const JPH::BodyID bodyId{command.payload.bodyId};
                JPH::BodyInterface& bodyInterface = ctx->physicsSystem->GetBodyInterface();
                bodyInterface.RemoveBody(bodyId);
                bodyInterface.DestroyBody(bodyId);
                break;
            }
            case CommandType::MeshRelease: {
                const MeshReleasePayload& release = command.payload.meshRelease;
                Core::RangeAllocator::Range range{release.rangeOffset, release.rangeCount};
                Core::RangeAllocator::Range modelRange{release.modelRangeOffset, release.modelRangeCount};
                state->instanceStore.ReleaseAndFree(ctx->materialManager, &state->triLightStore, range);
                if (modelRange.IsValid()) {
                    state->modelStore.Free(modelRange);
                }
                if (release.modelHandle.IsValid()) {
                    ctx->assetManager->UnloadModel(release.modelHandle);
                }
                break;
            }
            case CommandType::AreaLightConstruct: {
                if (state->registry.valid(command.entity)) {
                    if (auto* light = state->registry.try_get<Component::AreaLightComponent>(command.entity)) {
                        if (light->lightSlot == AnalyticLightStore::INVALID_SLOT) {
                            light->lightSlot = state->analyticLightStore.Allocate();
                        }
                    }
                }
                break;
            }
            case CommandType::SphereLightConstruct: {
                if (state->registry.valid(command.entity)) {
                    if (auto* light = state->registry.try_get<Component::SphereLightComponent>(command.entity)) {
                        if (light->lightSlot == AnalyticLightStore::INVALID_SLOT) {
                            light->lightSlot = state->analyticLightStore.Allocate();
                        }
                    }
                }
                break;
            }
            case CommandType::LightSlotFree: {
                uint32_t slot = command.payload.lightSlot;
                if (slot != AnalyticLightStore::INVALID_SLOT) {
                    state->analyticLightStore.Free(slot);
                }
                break;
            }
            case CommandType::CubemapRelease: {
                ctx->assetManager->UnloadCubemap(command.payload.cubemapHandle);
                break;
            }
            case CommandType::FontRelease: {
                ctx->assetManager->UnloadFont(command.payload.fontHandle);
                break;
            }
            case CommandType::ProbeConstruct: {
                if (state->registry.valid(command.entity) && state->registry.all_of<Component::ReflectionProbeComponent>(command.entity)) {
                    Component::ReflectionProbeComponent::DeferredConstruct(state->registry, command.entity);
                }
                break;
            }
            case CommandType::TextConstruct: {
                if (state->registry.valid(command.entity)) {
                    if (auto* text = state->registry.try_get<Component::TextComponent>(command.entity)) {
                        Component::LoadTextComponent(*text, state->registry, command.entity);
                    }
                }
                break;
            }
            case CommandType::DestroyEntity: {
                if (state->registry.valid(command.entity)) {
                    state->registry.destroy(command.entity);
                }
                break;
            }
        }
    }
    commands.Clear();
}
} // Engine
