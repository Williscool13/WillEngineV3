//
// Created by William on 2026-10-17.
//

#include "breakable_systems.h"

#include <cmath>
#include <initializer_list>
#include <utility>

#include <tracy/Tracy.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/containers/arena_fixed_vector.h"
#include "core/time/time_frame.h"
#include "engine/asset_manager.h"
#include "engine/components/camera_components.h"
#include "engine/console/console.h"
#include "engine/engine_api.h"
#include "engine/components/common_components.h"
#include "engine/components/physics/physics_body_component.h"
#include "engine/components/physics/physics_body_desc.h"
#include "engine/components/physics/physics_components.h"
#include "engine/components/render/static_mesh_component.h"
#include "engine/include/engine_context.h"
#include "engine/resources/physics/physics_collider_asset.h"
#include "engine/systems/command_queue.h"
#include "engine/systems/scene_system.h"
#include "game/ballance/lava/lava_component.h"
#include "platform/file_utils.h"
#include "game/ballance/fwd_components.h"
#include "game/ballance/breakable/breakable_component.h"
#include "physics/physics_world.h"
#include "physics/physics_layers.h"

namespace Ballance
{
static glm::mat4 NodeModelSpace(const Core::HeapArray<Engine::Node>& nodes, uint32_t nodeIndex)
{
    glm::mat4 m(1.0f);
    for (uint32_t n = nodeIndex; n != ~0u; n = nodes[n].parent) {
        const Engine::Node& node = nodes[n];
        m = glm::translate(glm::mat4(1.0f), node.localTranslation) * glm::mat4_cast(node.localRotation) * glm::scale(glm::mat4(1.0f), node.localScale) * m;
    }
    return m;
}

/** v turned toward perpendicular just enough that its direction is at most maxCos along axis (unit); its length is kept. */
static glm::vec3 LimitAlignment(const glm::vec3& v, const glm::vec3& axis, float maxCos)
{
    const float length = glm::length(v);
    if (length < 1e-6f) { return v; }
    const float c = glm::dot(v, axis) / length;
    if (std::abs(c) <= maxCos) { return v; }
    const glm::vec3 perp = v - c * length * axis;
    const float perpLength = glm::length(perp);
    const glm::vec3 side = perpLength > 1e-6f ? perp / perpLength : glm::normalize(glm::cross(axis, std::abs(axis.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f)));
    return length * (std::copysign(maxCos, c) * axis + std::sqrt(1.0f - maxCos * maxCos) * side);
}

static void ReleaseBreakableRuntime(Engine::AssetManager* assetManager, Component::BreakableRuntime& runtime)
{
    if (runtime.model.IsValid()) { assetManager->UnloadModel(runtime.model); }
    for (const Engine::PhysicsColliderHandle handle : runtime.colliders) {
        if (handle.IsValid()) { assetManager->UnloadCollider(handle); }
    }
    runtime.model = {};
    runtime.colliders.Clear();
}

static void DropBreakableRuntime(entt::registry& registry, entt::entity entity)
{
    registry.remove<Component::BreakableRuntime>(entity);
}

static void BreakNearCamera(Engine::EngineContext* ctx, Engine::EngineState* state, Core::Span<const char*>)
{
    constexpr float RADIUS = 10.0f;
    if (!Engine::IsRunning(state)) {
        Engine::Console::Print(state, "  b.crate.break: needs play or simulate");
        return;
    }
    const auto* camera = state->registry.try_get<Component::CameraRuntime>(state->renderedCamera);
    if (!camera) {
        Engine::Console::Print(state, "  b.crate.break: no rendered camera");
        return;
    }
    const glm::vec3 origin = camera->currentViewData.cameraPos;

    auto view = state->registry.view<Component::BreakableComponent>();
    auto nearby = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), std::max(view.size(), size_t{1}));
    for (const entt::entity entity : view) {
        if (glm::distance(Component::ComputeWorldTransform(state->registry, entity).translation, origin) <= RADIUS) {
            nearby.PushBack(entity);
        }
    }
    uint32_t broken = 0;
    for (const entt::entity entity : nearby) {
        if (Break(ctx, state, entity)) { ++broken; }
    }
    Engine::Console::Print(state, Core::InlineString<256>::Format("  broke %u of %zu breakables within %.0f m", broken, nearby.Size(), RADIUS).c_str());
}

static void AppendPose(Core::InlineString<384>& line, const Transform& world)
{
    line.Append(Core::InlineString<256>::Format(" %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g", world.translation.x, world.translation.y, world.translation.z,
        world.rotation.w, world.rotation.x, world.rotation.y, world.rotation.z, world.scale.x, world.scale.y, world.scale.z));
}

static void DumpSeed(Engine::EngineContext* ctx, Engine::EngineState* state, Core::Span<const char*> args)
{
    if (args.Size() < 2) {
        Engine::Console::Print(state, "  usage: b.seed.dump <path>");
        return;
    }
    entt::registry& registry = state->registry;
    auto crates = registry.view<Component::BreakableComponent, Component::PrefabInstanceComponent, Component::DoNotSerializeTag>(entt::exclude<Component::DissolvingRuntime>);
    auto shards = registry.view<Component::ShardTag, Component::StaticMeshComponent, Component::PhysicsBodyDesc>(entt::exclude<Component::DissolvingRuntime>);
    constexpr size_t LINE_BYTES = 384;
    auto text = Core::ArenaFixedVector<char>(&ctx->gameplayArena.Get(), (crates.size_hint() + shards.size_hint() + 1) * LINE_BYTES);
    auto append = [&](const Core::InlineString<384>& line) {
        for (size_t i = 0; i < line.Size(); ++i) { text.PushBack(line.c_str()[i]); }
        text.PushBack('\n');
    };

    uint32_t crateCount = 0;
    for (const auto& [entity, breakable, prefab] : crates.each()) {
        auto line = Core::InlineString<384>::Format("crate %llu", static_cast<unsigned long long>(prefab.prefabId.id));
        AppendPose(line, Component::ComputeWorldTransform(registry, entity));
        append(line);
        ++crateCount;
    }
    uint32_t shardCount = 0;
    for (const auto& [entity, mesh, desc] : shards.each()) {
        auto line = Core::InlineString<384>::Format("shard %llu %u %.9g %.9g %.9g", static_cast<unsigned long long>(mesh.modelId.id), mesh.nodeIndex, desc.mass, desc.friction, desc.restitution);
        AppendPose(line, Component::ComputeWorldTransform(registry, entity));
        append(line);
        ++shardCount;
    }

    const bool bWritten = Platform::WriteFile(Core::Path(args[1]), std::string_view(text.Data(), text.Size()));
    Engine::Console::Print(state, Core::InlineString<256>::Format("  %s %u crates, %u shards to %s", bWritten ? "wrote" : "FAILED writing", crateCount, shardCount, args[1]).c_str());
}

void RegisterBreakableCommands(Engine::EngineState* state)
{
    Engine::Console::Register(state, Engine::Origin::Game, "b.crate.break", "Breaks every breakable within 10 m of the camera (play or simulate)", &BreakNearCamera);
    Engine::Console::Register(state, Engine::Origin::Game, "b.seed.dump", "`b.seed.dump <path>` writes every spawned intact crate and every shard (model, node, body, world pose) for seeding the scene", &DumpSeed);
}

void ConnectBreakableObservers(entt::registry& registry)
{
    registry.on_destroy<Component::BreakableRuntime>().connect<&Component::BreakableRuntime::OnDestroy>();
    registry.on_update<Component::BreakableComponent>().connect<&DropBreakableRuntime>();
    registry.on_destroy<Component::BreakableComponent>().connect<&DropBreakableRuntime>();
}

void DisconnectBreakableObservers(entt::registry& registry)
{
    registry.on_destroy<Component::BreakableRuntime>().disconnect<&Component::BreakableRuntime::OnDestroy>();
    registry.on_update<Component::BreakableComponent>().disconnect<&DropBreakableRuntime>();
    registry.on_destroy<Component::BreakableComponent>().disconnect<&DropBreakableRuntime>();
}

void AcquireBreakableAssets(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto view = state->registry.view<Component::BreakableComponent>(entt::exclude<Component::BreakableRuntime>);
    if (view.size_hint() == 0) { return; }

    Engine::AssetManager* assetManager = ctx->assetManager;
    auto acquired = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (const auto& [entity, breakable] : view.each()) {
        const auto* meta = assetManager->GetModelMetadata(breakable.brokenModelId);
        if (!meta || assetManager->IsModelFrozen(breakable.brokenModelId)) { continue; }
        acquired.PushBack(entity);
    }
    for (const entt::entity entity : acquired) {
        const Engine::ModelID brokenModelId = state->registry.get<Component::BreakableComponent>(entity).brokenModelId;
        const auto* meta = assetManager->GetModelMetadata(brokenModelId);
        Component::BreakableRuntime runtime{.model = assetManager->LoadModel(brokenModelId)};
        for (uint32_t n = 0; n < meta->nodes.Size() && !runtime.colliders.IsFull(); ++n) {
            if (meta->nodes[n].meshIndex == ~0u) { continue; }
            runtime.colliders.PushBack(assetManager->LoadModelCollider(brokenModelId, Engine::PhysicsColliderKind::ConvexHull, n));
        }
        state->registry.emplace<Component::BreakableRuntime>(entity, runtime);
    }
}

void UpdateSelfDestruct(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto view = state->registry.view<Component::SelfDestructComponent, Component::BreakableComponent>();
    if (view.size_hint() == 0) { return; }

    const float dt = state->timeFrame->deltaTime;
    auto due = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (const auto& [entity, selfDestruct, breakable] : view.each()) {
        auto& run = state->registry.get_or_emplace<Component::SelfDestructRuntime>(entity);
        run.elapsed += dt;
        if (run.elapsed >= selfDestruct.delay) { due.PushBack(entity); }
    }
    for (const entt::entity entity : due) {
        if (Break(ctx, state, entity)) {
            state->registry.remove<Component::SelfDestructComponent, Component::SelfDestructRuntime>(entity);
        }
    }
}

void ShatterOnContact(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    entt::registry& registry = state->registry;
    if (registry.view<Component::ShatterOnContactTag>().size() == 0) { return; }

    for (const auto& event : state->physics.resolvedAddedEvents) {
        for (const auto [shatterer, other] : {std::pair{event.e1, event.e2}, std::pair{event.e2, event.e1}}) {
            if (shatterer == entt::null || other == entt::null || !registry.all_of<Component::ShatterOnContactTag>(shatterer)) { continue; }
            if (registry.all_of<Component::BreakableComponent>(other)) { Break(ctx, state, other); }
        }
    }
}

bool Break(Engine::EngineContext* ctx, Engine::EngineState* state, entt::entity entity)
{
    ZoneScoped;
    entt::registry& registry = state->registry;
    const auto* breakable = registry.try_get<Component::BreakableComponent>(entity);
    const auto* runtime = registry.try_get<Component::BreakableRuntime>(entity);
    if (!breakable || !runtime) { return false; }
    const Engine::StaticModel* brokenModel = ctx->assetManager->GetModel(runtime->model);
    if (!brokenModel || brokenModel->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) { return false; }

    const Core::HeapArray<Engine::Node>& nodes = brokenModel->modelData.nodes;
    const Core::HeapArray<Engine::MeshInformation>& meshes = brokenModel->modelData.meshes;
    uint32_t shardCount = 0;
    for (const Engine::Node& node : nodes) {
        if (node.meshIndex < meshes.Size()) { ++shardCount; }
    }
    if (shardCount == 0) { return false; }

    const Engine::ModelID brokenModelId = breakable->brokenModelId;
    const float scatterSpeed = breakable->scatterSpeed;
    const float scatterSpin = breakable->scatterSpin;

    const Transform world = Component::ComputeWorldTransform(registry, entity);
    const glm::mat4 worldMatrix = world.GetMatrix();

    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    glm::vec3 centerOfMass = world.translation;
    if (const auto* body = Component::FindBody(registry, entity)) {
        linearVelocity = ctx->physicsWorld->GetLinearVelocity(body->bodyID);
        angularVelocity = ctx->physicsWorld->GetAngularVelocity(body->bodyID);
        centerOfMass = ctx->physicsWorld->GetCenterOfMassPosition(body->bodyID);
    }

    const auto* desc = registry.try_get<Component::PhysicsBodyDesc>(entity);
    const float shardMass = (desc ? desc->mass : 1.0f) / static_cast<float>(shardCount);
    const float friction = desc ? desc->friction : 0.5f;
    const float restitution = desc ? desc->restitution : 0.0f;

    constexpr float MAX_THIN_AXIS_SPIN_ALIGNMENT = 0.5f;

    const auto* name = registry.try_get<Component::NameComponent>(entity);
    for (uint32_t n = 0; n < nodes.Size(); ++n) {
        const uint32_t meshIndex = nodes[n].meshIndex;
        if (meshIndex >= meshes.Size()) { continue; }

        const glm::mat4 m = worldMatrix * NodeModelSpace(nodes, n);
        const glm::vec3 position{m[3]};
        const glm::vec3 scale{glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))};
        const glm::quat rotation = glm::quat_cast(glm::mat3(glm::vec3(m[0]) / scale.x, glm::vec3(m[1]) / scale.y, glm::vec3(m[2]) / scale.z));

        const entt::entity shard = Engine::CreateSceneEntity(state);
        registry.emplace<Component::DoNotSerializeTag>(shard);
        registry.emplace<Component::ShardTag>(shard);
        registry.get<Component::NameComponent>(shard).name = Core::InlineString<128>::Format("%s %s", name ? name->name.c_str() : "Broken", nodes[n].name.c_str());

        auto& transform = registry.get<Component::TransformComponent>(shard);
        transform.translation = position;
        transform.rotation = rotation;
        transform.scale = scale;
        transform.mobility = Component::Mobility::Movable;

        registry.emplace<Component::StaticMeshComponent>(shard, Component::StaticMeshComponent{.modelId = brokenModelId, .nodeIndex = n});

        Component::PhysicsBodyDesc shardDesc{
            .motionType = Component::PhysicsMotionType::Dynamic,
            .mass = shardMass,
            .friction = friction,
            .restitution = restitution,
            .motionQuality = Component::PhysicsMotionQuality::LinearCast,
            .layerOverride = Physics::Layers::DEBRIS,
            .bEnhancedInternalEdgeRemoval = true,
        };
        shardDesc.shapes.PushBack(Component::PhysicsShapeDesc{.geometry = Component::ColliderShape{.meshSourceModelId = brokenModelId, .meshSourceNodeIndex = n}, .bakedScale = scale});
        registry.emplace<Component::PhysicsBodyDesc>(shard, shardDesc);

        glm::vec3 boundsMin(1e30f);
        glm::vec3 boundsMax(-1e30f);
        for (const Engine::PrimitiveProperty& primitive : meshes[meshIndex].primitiveProperties) {
            boundsMin = glm::min(boundsMin, primitive.boundingBoxMin);
            boundsMax = glm::max(boundsMax, primitive.boundingBoxMax);
        }
        const glm::vec3 extent = (boundsMax - boundsMin) * scale;
        const int thin = extent.x <= extent.y && extent.x <= extent.z ? 0 : (extent.y <= extent.z ? 1 : 2);
        glm::vec3 thinAxis(0.0f);
        thinAxis[thin] = 1.0f;
        const glm::vec3 spin = LimitAlignment(state->rng.UnitVector() * (scatterSpin * state->rng.Range(0.5f, 1.0f)), rotation * thinAxis, MAX_THIN_AXIS_SPIN_ALIGNMENT);

        const glm::vec3 offset = position - centerOfMass;
        const float offsetLength = glm::length(offset);
        registry.emplace<Component::SetVelocityRequest>(shard, Component::SetVelocityRequest{
            .linearVelocity = linearVelocity + glm::cross(angularVelocity, offset) + (offsetLength > 1e-4f ? scatterSpeed / offsetLength : 0.0f) * offset,
            .angularVelocity = angularVelocity + spin,
        });
    }

    registry.remove<Component::BreakableComponent>(entity);
    state->commandQueue.Push({.type = Engine::CommandType::DestroyEntity, .entity = entity});
    return true;
}
} // Ballance

namespace Ballance::Component
{
void BreakableRuntime::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    ReleaseBreakableRuntime(ctx->assetManager, registry.get<BreakableRuntime>(entity));
}
} // Ballance::Component
