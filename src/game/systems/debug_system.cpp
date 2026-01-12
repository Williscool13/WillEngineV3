//
// Created by William on 2025-12-22.
//

#include "debug_system.h"

#include <Jolt/Jolt.h>
#include <spdlog/spdlog.h>

#include "asset-load/asset_load_config.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"

#include "core/include/engine_context.h"
#include "core/input/input_frame.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/fwd_components.h"
#include "game/components/gameplay/anti_gravity_component.h"
#include "game/components/gameplay/floor_component.h"
#include "game/components/physics/dynamic_physics_body_component.h"
#include "physics/physics_system.h"
#include "platform/paths.h"


namespace Game::System
{
static Engine::WillModelHandle dragonHandle = Engine::WillModelHandle::INVALID;
static Engine::WillModelHandle boxHandle = Engine::WillModelHandle::INVALID;
static Engine::WillModelHandle box4kHandle = Engine::WillModelHandle::INVALID;
static Engine::WillModelHandle sponzaHandle = Engine::WillModelHandle::INVALID;
static Engine::TextureHandle textureHandle = Engine::TextureHandle::INVALID;
static JPH::BodyID boxBodyID;
static JPH::BodyID floorBodyID;
static Engine::MaterialID boxMatID;

void CreateBox(Core::EngineContext* ctx, Engine::GameState* state, glm::vec3 position)
{
    if (!boxHandle.IsValid()) {
        SPDLOG_WARN("[DebugSystem] No box model loaded, press F1 first");
        return;
    }

    auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();

    JPH::BoxShapeSettings boxShapeSettings(JPH::Vec3(0.5f, 0.5f, 0.5f));
    boxShapeSettings.SetDensity(12.5f);
    boxShapeSettings.SetEmbedded();
    JPH::ShapeSettings::ShapeResult boxShapeResult = boxShapeSettings.Create();
    JPH::ShapeRefC boxShape = boxShapeResult.Get();

    JPH::BodyCreationSettings boxSettings(
        boxShape,
        JPH::RVec3(position.x, position.y, position.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Physics::Layers::MOVING
    );

    boxBodyID = bodyInterface.CreateAndAddBody(boxSettings, JPH::EActivation::Activate);

    Render::WillModel* model = ctx->assetManager->GetModel(boxHandle);
    if (!model || model->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
        SPDLOG_WARN("[DebugSystem] Model not ready yet");
        return;
    }

    RenderableComponent renderable{};
    Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();
    Render::MeshInformation& submesh = model->modelData.meshes[0];

    for (size_t i = 0; i < submesh.primitiveProperties.size(); ++i) {
        Render::PrimitiveProperty& primitive = submesh.primitiveProperties[i];

        MaterialProperties material;
        if (primitive.materialIndex != -1) {
            material = model->modelData.materials[primitive.materialIndex];
        }
        else {
            material = materialManager.Get(materialManager.GetDefaultMaterial());
        }
        // material.textureImageIndices.x = 3;
        boxMatID = materialManager.GetOrCreate(material);

        renderable.primitives[i] = {
            .primitiveIndex = primitive.index,
            .materialID = boxMatID
        };
    }
    renderable.primitiveCount = submesh.primitiveProperties.size();
    renderable.modelFlags = glm::vec4(0.0f);

    entt::entity boxEntity = state->registry.create();
    state->registry.emplace<RenderableComponent>(boxEntity, renderable);
    TransformComponent transformComponent = state->registry.emplace<TransformComponent>(boxEntity, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    state->registry.emplace<PhysicsBodyComponent>(boxEntity, boxBodyID);
    state->registry.emplace<DynamicPhysicsBodyComponent>(boxEntity, transformComponent.translation, transformComponent.rotation);
}

entt::entity CreateStaticBox(Core::EngineContext* ctx, Engine::GameState* state,
                             JPH::RVec3 physicsPos, JPH::Vec3 halfExtents,
                             glm::vec3 renderPos, glm::vec3 renderScale,
                             glm::vec4 color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f))
{
    auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();

    // Create physics body
    JPH::BoxShapeSettings shapeSettings(halfExtents);
    shapeSettings.SetEmbedded();
    JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
    JPH::ShapeRefC shape = shapeResult.Get();

    JPH::BodyCreationSettings bodySettings(
        shape,
        physicsPos,
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Physics::Layers::NON_MOVING
    );

    JPH::BodyID bodyID = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);

    // Create renderable
    Render::WillModel* model = ctx->assetManager->GetModel(boxHandle);
    if (!model || model->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
        SPDLOG_WARN("[CreateStaticBox] Model not ready yet");
        return entt::null;
    }

    RenderableComponent renderable{};
    Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();
    Render::MeshInformation& submesh = model->modelData.meshes[0];

    for (size_t i = 0; i < submesh.primitiveProperties.size(); ++i) {
        Render::PrimitiveProperty& primitive = submesh.primitiveProperties[i];

        MaterialProperties material;
        if (primitive.materialIndex != -1) {
            material = model->modelData.materials[primitive.materialIndex];
        }
        else {
            material = materialManager.Get(materialManager.GetDefaultMaterial());
        }
        // material.textureImageIndices.x = AssetLoad::ERROR_IMAGE_BINDLESS_INDEX;
        // material.textureSamplerIndices.x = AssetLoad::DEFAULT_SAMPLER_BINDLESS_INDEX;
        Engine::MaterialID matID = materialManager.GetOrCreate(material);

        renderable.primitives[i] = {
            .primitiveIndex = primitive.index,
            .materialID = matID
        };
    }
    renderable.primitiveCount = submesh.primitiveProperties.size();
    renderable.modelFlags = glm::vec4(0.0f);

    // Create entity
    entt::entity entity = state->registry.create();
    state->registry.emplace<RenderableComponent>(entity, renderable);

    TransformComponent transform;
    transform.translation = renderPos;
    transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    transform.scale = renderScale;
    state->registry.emplace<TransformComponent>(entity, transform);
    state->registry.emplace<PhysicsBodyComponent>(entity, bodyID);

    return entity;
}

void DebugUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    if (state->inputFrame->GetKey(Key::F1).pressed) {
        if (!dragonHandle.IsValid()) {
            dragonHandle = ctx->assetManager->LoadModel(Platform::GetAssetPath() / "dragon/dragon.willmodel");
            //boxHandle = ctx->assetManager->LoadModel(Platform::GetAssetPath() / "BoxTextured.willmodel");
            boxHandle = ctx->assetManager->LoadModel(Platform::GetAssetPath() / "BoxTextured4k.willmodel");
            // box4kHandle = ctx->assetManager->LoadModel(Platform::GetAssetPath() / "BoxTextured4k.willmodel");
            sponzaHandle = ctx->assetManager->LoadModel(Platform::GetAssetPath() / "sponza2/sponza.willmodel");
        }
    }

    if (state->inputFrame->GetKey(Key::F2).pressed) {
        Render::WillModel* box = ctx->assetManager->GetModel(boxHandle);
        if (!box || box->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
            SPDLOG_WARN("[DebugSystem] Box model not ready yet");
            return;
        }

        textureHandle = ctx->assetManager->LoadTexture(Platform::GetAssetPath() / "textures/smiling_friend.ktx2");

        entt::entity floor = CreateStaticBox(
            ctx, state,
            JPH::RVec3(0.0, -0.5, 0.0), JPH::Vec3(10.0f, 0.5f, 10.0f), // physics
            glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f), // render
            glm::vec4(0.5f, 0.5f, 0.5f, 1.0f) // gray
        );
        state->registry.emplace<FloorComponent>(floor);

        // Create walls
        CreateStaticBox(ctx, state, JPH::RVec3(0, 2.5, -10), JPH::Vec3(10, 2.5, 0.5),
                        glm::vec3(0, 2.5, -10), glm::vec3(20, 5, 1)); // back
        CreateStaticBox(ctx, state, JPH::RVec3(0, 2.5, 10), JPH::Vec3(10, 2.5, 0.5),
                        glm::vec3(0, 2.5, 10), glm::vec3(20, 5, 1)); // front
        CreateStaticBox(ctx, state, JPH::RVec3(-10, 2.5, 0), JPH::Vec3(0.5, 2.5, 10),
                        glm::vec3(-10, 2.5, 0), glm::vec3(1, 5, 20)); // left
        CreateStaticBox(ctx, state, JPH::RVec3(10, 2.5, 0), JPH::Vec3(0.5, 2.5, 10),
                        glm::vec3(10, 2.5, 0), glm::vec3(1, 5, 20)); // right

        for (int i = 0; i < 5; i++) {
            glm::vec3 spawnPos = glm::vec3(i * 2.0f - 4.0f, 5.0f, 0.0f);
            CreateBox(ctx, state, spawnPos);
        }

        SPDLOG_INFO("[DebugSystem] Created physics floor and arena walls");
        SPDLOG_INFO("[DebugSystem] Created falling boxes");
    }

    if (state->inputFrame->GetKey(Key::F3).pressed) {
        Render::WillModel* dragon = ctx->assetManager->GetModel(dragonHandle);
        if (!dragon || dragon->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
            SPDLOG_WARN("[DebugSystem] Dragon model not ready yet");
            return;
        }

        Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();
        Render::MeshInformation& submesh = dragon->modelData.meshes[0];
        glm::vec3 meshOffset = glm::vec3(0.0f);
        glm::quat meshRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 meshScale = glm::vec3(1.0f);

        for (size_t i = 0; i < dragon->modelData.nodes.size(); ++i) {
            const auto& node = dragon->modelData.nodes[i];
            if (node.meshIndex == 0) {
                meshOffset = node.localTranslation;
                meshRotation = node.localRotation;
                meshScale = node.localScale;

                uint32_t parentIdx = node.parent;
                while (parentIdx != ~0u) {
                    const auto& parentNode = dragon->modelData.nodes[parentIdx];
                    meshOffset = parentNode.localRotation * (parentNode.localScale * meshOffset) + parentNode.localTranslation;
                    meshRotation = parentNode.localRotation * meshRotation;
                    meshScale = parentNode.localScale * meshScale;
                    parentIdx = parentNode.parent;
                }
                break;
            }
        }

        std::vector<glm::vec3> dragonPositions = {
            glm::vec3(-7.0f, 1.0f, -7.0f) + meshOffset,
            glm::vec3(7.0f, 1.0f, 7.0f) + meshOffset,
            glm::vec3(0.0f, 1.0f, -7.0f) + meshOffset
        };

        for (const auto& pos : dragonPositions) {
            RenderableComponent renderable{};

            for (size_t i = 0; i < submesh.primitiveProperties.size(); ++i) {
                Render::PrimitiveProperty& primitive = submesh.primitiveProperties[i];

                Engine::MaterialID matID;
                if (primitive.materialIndex == -1) {
                    matID = materialManager.GetDefaultMaterial();
                }
                else {
                    matID = materialManager.GetOrCreate(dragon->modelData.materials[primitive.materialIndex]);
                }

                renderable.primitives[i] = {
                    .primitiveIndex = primitive.index,
                    .materialID = matID
                };
            }
            renderable.primitiveCount = submesh.primitiveProperties.size();
            renderable.modelFlags = glm::vec4(0.0f);

            entt::entity dragonEntity = state->registry.create();
            state->registry.emplace<RenderableComponent>(dragonEntity, renderable);
            state->registry.emplace<TransformComponent>(dragonEntity, pos, meshRotation, meshScale * 1.5f);
        }

        SPDLOG_INFO("[DebugSystem] Spawned dragons around arena");
    }

    if (state->inputFrame->GetKey(Key::F4).pressed) {
        Render::WillModel* sponza = ctx->assetManager->GetModel(sponzaHandle);
        if (!sponza || sponza->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
            SPDLOG_WARN("[DebugSystem] Sponza model not ready yet");
            return;
        }

        Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();

        std::vector<glm::vec3> worldTranslations(sponza->modelData.nodes.size());
        std::vector<glm::quat> worldRotations(sponza->modelData.nodes.size());
        std::vector<glm::vec3> worldScales(sponza->modelData.nodes.size());

        for (size_t i = 0; i < sponza->modelData.nodes.size(); ++i) {
            const auto& node = sponza->modelData.nodes[i];

            if (node.parent == ~0u) {
                worldTranslations[i] = node.localTranslation;
                worldRotations[i] = node.localRotation;
                worldScales[i] = node.localScale;
            }
            else {
                const glm::vec3& parentT = worldTranslations[node.parent];
                const glm::quat& parentR = worldRotations[node.parent];
                const glm::vec3& parentS = worldScales[node.parent];

                worldTranslations[i] = parentR * (parentS * node.localTranslation) + parentT;
                worldRotations[i] = parentR * node.localRotation;
                worldScales[i] = parentS * node.localScale;
            }
        }

        for (size_t i = 0; i < sponza->modelData.nodes.size(); ++i) {
            const auto& node = sponza->modelData.nodes[i];
            if (node.meshIndex == ~0u) continue;

            Render::MeshInformation& mesh = sponza->modelData.meshes[node.meshIndex];

            if (mesh.primitiveProperties.size() > 128) {
                SPDLOG_WARN("[DebugSystem] Node {} has {} primitives, limiting to 128", i, mesh.primitiveProperties.size());
            }

            RenderableComponent renderable{};
            size_t primCount = std::min(mesh.primitiveProperties.size(), static_cast<size_t>(128));

            for (size_t j = 0; j < primCount; ++j) {
                Render::PrimitiveProperty& primitive = mesh.primitiveProperties[j];

                Engine::MaterialID matID;
                if (primitive.materialIndex == -1) {
                    matID = materialManager.GetDefaultMaterial();
                }
                else {
                    matID = materialManager.GetOrCreate(sponza->modelData.materials[primitive.materialIndex]);
                }

                renderable.primitives[j] = {
                    .primitiveIndex = primitive.index,
                    .materialID = matID
                };
            }
            renderable.primitiveCount = primCount;
            renderable.modelFlags = glm::vec4(0.0f);

            entt::entity sponzaEntity = state->registry.create();
            state->registry.emplace<RenderableComponent>(sponzaEntity, renderable);
            state->registry.emplace<TransformComponent>(sponzaEntity, worldTranslations[i], worldRotations[i], worldScales[i]);
        }

        SPDLOG_INFO("[DebugSystem] Spawned sponza");
    }

    if (state->inputFrame->GetKey(Key::F6).pressed) {
        Render::WillModel* dragon = ctx->assetManager->GetModel(dragonHandle);
        if (!dragon || dragon->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
            SPDLOG_WARN("[DebugSystem] Dragon model not ready yet");
            return;
        }

        Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();
        Render::MeshInformation& submesh = dragon->modelData.meshes[0];
        glm::vec3 meshOffset = glm::vec3(0.0f);
        glm::quat meshRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 meshScale = glm::vec3(1.0f);

        for (size_t i = 0; i < dragon->modelData.nodes.size(); ++i) {
            const auto& node = dragon->modelData.nodes[i];
            if (node.meshIndex == 0) {
                meshOffset = node.localTranslation;
                meshRotation = node.localRotation;
                meshScale = node.localScale;

                uint32_t parentIdx = node.parent;
                while (parentIdx != ~0u) {
                    const auto& parentNode = dragon->modelData.nodes[parentIdx];
                    meshOffset = parentNode.localRotation * (parentNode.localScale * meshOffset) + parentNode.localTranslation;
                    meshRotation = parentNode.localRotation * meshRotation;
                    meshScale = parentNode.localScale * meshScale;
                    parentIdx = parentNode.parent;
                }
                break;
            }
        }

        // Create massive shadow test floor
        entt::entity shadowFloor = CreateStaticBox(
            ctx, state,
            JPH::RVec3(0.0, -0.5, 0.0), JPH::Vec3(50.0f, 0.5f, 50.0f), // physics
            glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(100.0f, 1.0f, 100.0f), // render
            glm::vec4(0.3f, 0.3f, 0.3f, 1.0f) // dark gray
        );

        // Create vertical column of dragons
        for (int y = 0; y < 10; ++y) {
            glm::vec3 pos = glm::vec3(0.0f, 1.0f + y * 3.0f, 0.0f) + meshOffset;

            RenderableComponent renderable{};

            for (size_t i = 0; i < submesh.primitiveProperties.size(); ++i) {
                Render::PrimitiveProperty& primitive = submesh.primitiveProperties[i];

                Engine::MaterialID matID;
                if (primitive.materialIndex == -1) {
                    matID = materialManager.GetDefaultMaterial();
                }
                else {
                    matID = materialManager.GetOrCreate(dragon->modelData.materials[primitive.materialIndex]);
                }

                renderable.primitives[i] = {
                    .primitiveIndex = primitive.index,
                    .materialID = matID
                };
            }
            renderable.primitiveCount = submesh.primitiveProperties.size();
            renderable.modelFlags = glm::vec4(0.0f);

            entt::entity dragonEntity = state->registry.create();
            state->registry.emplace<RenderableComponent>(dragonEntity, renderable);
            state->registry.emplace<TransformComponent>(dragonEntity, pos, meshRotation, meshScale * 1.5f);
        }

        SPDLOG_INFO("[DebugSystem] Created PCSS test scene: 100x100 floor + vertical dragon column");
    }

    if (ctx->bImguiKeyboardCaptured) { return; }
    if (state->inputFrame->GetKey(Key::NUM_1).pressed) {
        auto view = state->registry.view<RenderDebugViewComponent>();
        for (auto [entity, debugViewComponent] : view.each()) {
            debugViewComponent.debugIndex = (debugViewComponent.debugIndex == 1) ? 0 : 1;
        }
    }

    if (state->inputFrame->GetKey(Key::NUM_2).pressed) {
        auto view = state->registry.view<RenderDebugViewComponent>();
        for (auto [entity, debugViewComponent] : view.each()) {
            debugViewComponent.debugIndex = (debugViewComponent.debugIndex == 2) ? 0 : 2;
        }
    }

    if (state->inputFrame->GetKey(Key::NUM_3).pressed) {
        auto view = state->registry.view<RenderDebugViewComponent>();
        for (auto [entity, debugViewComponent] : view.each()) {
            debugViewComponent.debugIndex = (debugViewComponent.debugIndex == 3) ? 0 : 3;
        }
    }

    if (state->inputFrame->GetKey(Key::NUM_4).pressed) {
        auto view = state->registry.view<RenderDebugViewComponent>();
        for (auto [entity, debugViewComponent] : view.each()) {
            debugViewComponent.debugIndex = (debugViewComponent.debugIndex == 4) ? 0 : 4;
        }
    }

    if (state->inputFrame->GetKey(Key::NUM_5).pressed) {
        auto view = state->registry.view<RenderDebugViewComponent>();
        for (auto [entity, debugViewComponent] : view.each()) {
            debugViewComponent.debugIndex = (debugViewComponent.debugIndex == 5) ? 0 : 5;
        }
    }
    if (state->inputFrame->GetKey(Key::NUM_6).pressed) {
        auto view = state->registry.view<RenderDebugViewComponent>();
        for (auto [entity, debugViewComponent] : view.each()) {
            debugViewComponent.debugIndex = (debugViewComponent.debugIndex == 6) ? 0 : 6;
        }
    }
    if (state->inputFrame->GetKey(Key::NUM_7).pressed) {
        auto view = state->registry.view<RenderDebugViewComponent>();
        for (auto [entity, debugViewComponent] : view.each()) {
            debugViewComponent.debugIndex = (debugViewComponent.debugIndex == 7) ? 0 : 7;
        }
    }
    if (state->inputFrame->GetKey(Key::NUM_8).pressed) {
        auto view = state->registry.view<RenderDebugViewComponent>();
        for (auto [entity, debugViewComponent] : view.each()) {
            debugViewComponent.debugIndex = (debugViewComponent.debugIndex == 8) ? 0 : 8;
        }
    }
    if (state->inputFrame->GetKey(Key::NUM_9).pressed) {
        auto view = state->registry.view<RenderDebugViewComponent>();
        for (auto [entity, debugViewComponent] : view.each()) {
            debugViewComponent.debugIndex = (debugViewComponent.debugIndex == 9) ? 0 : 9;
        }
    }

    if (state->inputFrame->GetKey(Key::I).pressed) {
        Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();
        MaterialProperties boxMat = materialManager.Get(boxMatID);
        boxMat.textureImageIndices.x = 0;
        materialManager.Update(boxMatID, boxMat);
    }
    if (state->inputFrame->GetKey(Key::O).pressed) {
        Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();
        MaterialProperties boxMat = materialManager.Get(boxMatID);
        boxMat.textureImageIndices.x--;
        materialManager.Update(boxMatID, boxMat);
    }
    if (state->inputFrame->GetKey(Key::P).pressed) {
        Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();
        MaterialProperties boxMat = materialManager.Get(boxMatID);
        boxMat.textureImageIndices.x++;
        materialManager.Update(boxMatID, boxMat);
    }

    if (state->inputFrame->GetKey(Key::C).pressed) {
        DebugVisualizeCascadeCorners(ctx, state);
    }
}

void DebugProcessPhysicsCollisions(Core::EngineContext* ctx, Engine::GameState* state)
{
    std::span<const Physics::DeferredCollisionEvent> events = ctx->physicsSystem->GetCollisionEvents();


    state->registry.clear<AntiGravityComponent>();

    for (const auto& event : events) {
        entt::entity entity1 = state->bodyToEntity.contains(event.body1)
                                   ? state->bodyToEntity[event.body1]
                                   : entt::null;
        entt::entity entity2 = state->bodyToEntity.contains(event.body2)
                                   ? state->bodyToEntity[event.body2]
                                   : entt::null;

        if (entity1 == entt::null || entity2 == entt::null) continue;

        if (state->registry.all_of<FloorComponent>(entity2)) {
            state->registry.emplace_or_replace<AntiGravityComponent>(entity1);
        }
        else if (state->registry.all_of<FloorComponent>(entity1)) {
            state->registry.emplace_or_replace<AntiGravityComponent>(entity2);
        }
    }

    ctx->physicsSystem->ClearCollisionEvents();
    ctx->physicsSystem->ClearActivationEvents();
}

void DebugApplyGroundForces(Core::EngineContext* ctx, Engine::GameState* state)
{
    auto view = state->registry.view<AntiGravityComponent, PhysicsBodyComponent>();
    auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();

    for (auto [entity, physics] : view.each()) {
        bodyInterface.AddImpulse(physics.bodyID, JPH::Vec3(0, 100.0f, 0));
    }
}

static std::array<glm::vec3, 8> GetPerspectiveFrustumCornersWorldSpace(
    float near, float far, float fovRadians, float aspectRatio,
    const glm::vec3& cameraPos, const glm::vec3& cameraForward)
{
    const float halfVSide = far * tanf(fovRadians * 0.5f);
    const float halfHSide = halfVSide * aspectRatio;
    const glm::vec3 frontMultFar = far * cameraForward;

    glm::vec3 right = glm::normalize(glm::cross(cameraForward, glm::vec3(0, 1, 0)));
    glm::vec3 up = glm::normalize(glm::cross(right, cameraForward));

    std::array<glm::vec3, 8> corners{};

    const float halfVSideNear = near * tanf(fovRadians * 0.5f);
    const float halfHSideNear = halfVSideNear * aspectRatio;
    const glm::vec3 frontMultNear = near * cameraForward;

    corners[0] = cameraPos + frontMultNear - up * halfVSideNear - right * halfHSideNear;
    corners[1] = cameraPos + frontMultNear + up * halfVSideNear - right * halfHSideNear;
    corners[2] = cameraPos + frontMultNear + up * halfVSideNear + right * halfHSideNear;
    corners[3] = cameraPos + frontMultNear - up * halfVSideNear + right * halfHSideNear;
    corners[4] = cameraPos + frontMultFar - up * halfVSide - right * halfHSide;
    corners[5] = cameraPos + frontMultFar + up * halfVSide - right * halfHSide;
    corners[6] = cameraPos + frontMultFar + up * halfVSide + right * halfHSide;
    corners[7] = cameraPos + frontMultFar - up * halfVSide + right * halfHSide;

    return corners;
}

void DebugVisualizeCascadeCorners(Core::EngineContext* ctx, Engine::GameState* state)
{
    if (!boxHandle.IsValid()) {
        SPDLOG_WARN("[DebugSystem] Load box model (F1) first");
        return;
    }

    Render::WillModel* box = ctx->assetManager->GetModel(boxHandle);
    if (!box || box->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
        SPDLOG_WARN("[DebugSystem] Box model not ready");
        return;
    }


    auto cameraView = state->registry.view<CameraComponent, TransformComponent>();
    auto [cameraEntity, camera, transform] = *cameraView.each().begin();
    Core::ViewData viewData = camera.currentViewData;
    float nearPlane = camera.currentViewData.nearPlane;
    float farPlane = camera.currentViewData.farPlane;

    const float ratio = farPlane / nearPlane;
    float nearSplits[4], farSplits[4];
    nearSplits[0] = nearPlane;

    for (size_t i = 1; i < 4; i++) {
        const float si = static_cast<float>(i) / 4.0f;
        const float lambda = 0.5f; // Split lambda
        const float uniformTerm = nearPlane + (farPlane - nearPlane) * si;
        const float logTerm = nearPlane * std::pow(ratio, si);
        nearSplits[i] = lambda * logTerm + (1.0f - lambda) * uniformTerm;
        farSplits[i - 1] = nearSplits[i] * 1.05f; // Split overlap
    }
    farSplits[3] = farPlane;

    glm::vec4 cascadeColors[4] = {
        glm::vec4(1, 0, 0, 1),
        glm::vec4(0, 1, 0, 1),
        glm::vec4(0, 0, 1, 1),
        glm::vec4(1, 0, 1, 1)
    };

    Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();
    Render::MeshInformation& submesh = box->modelData.meshes[0];

    for (int cascade = 0; cascade < 4; ++cascade) {
        std::array<glm::vec3, 8> corners = GetPerspectiveFrustumCornersWorldSpace(
            nearSplits[cascade], farSplits[cascade],
            viewData.fovRadians, viewData.aspectRatio,
            viewData.cameraPos, viewData.cameraForward
        );

        MaterialProperties material = materialManager.Get(materialManager.GetDefaultMaterial());
        material.colorFactor = cascadeColors[cascade];
        Engine::MaterialID matID = materialManager.GetOrCreate(material);

        for (int i = 0; i < 8; ++i) {
            RenderableComponent renderable{};

            for (size_t j = 0; j < submesh.primitiveProperties.size(); ++j) {
                renderable.primitives[j] = {
                    .primitiveIndex = submesh.primitiveProperties[j].index,
                    .materialID = matID
                };
            }
            renderable.primitiveCount = submesh.primitiveProperties.size();
            renderable.modelFlags = glm::vec4(0.0f);

            entt::entity cornerEntity = state->registry.create();
            state->registry.emplace<RenderableComponent>(cornerEntity, renderable);
            state->registry.emplace<TransformComponent>(
                cornerEntity,
                corners[i],
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec3(1.0f)
            );
        }
    }

    SPDLOG_INFO("[DebugSystem] Spawned cascade corner markers");
}
} // Game::System
