//
// Created by William on 2025-12-22.
//

#include "debug_system.h"

#include <Jolt/Jolt.h>
#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include "audio/audio_manager.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"

#include "core/include/engine_context.h"
#include "core/input/input_frame.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/fwd_components.h"
#include "physics/physics_system.h"
#include "platform/paths.h"


namespace Game::System
{
static Engine::WillModelHandle dragonHandle = Engine::WillModelHandle::INVALID;
static Engine::WillModelHandle boxHandle = Engine::WillModelHandle::INVALID;
static Engine::WillModelHandle sphereHandle = Engine::WillModelHandle::INVALID;
static Engine::WillModelHandle sponzaHandle = Engine::WillModelHandle::INVALID;
static Engine::TextureHandle textureHandle = Engine::TextureHandle::INVALID;
static Engine::CubemapHandle cubemapHandle = Engine::CubemapHandle::INVALID;
static Engine::MaterialID boxMatID;

entt::entity CreateTextureVisualizer(Core::EngineContext* ctx, Engine::GameState* state,
                                     glm::vec3 renderPos, glm::vec3 renderScale)
{
    ZoneScoped;

    Render::WillModel* model = ctx->assetManager->GetModel(state->portalPlaneHandle);
    if (!model || model->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
        SPDLOG_WARN("[CreateTextureVisualizer] Model not ready yet");
        return entt::null;
    }

    Component::RenderableComponent renderable{};
    Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();
    Render::MeshInformation& submesh = model->modelData.meshes[0];

    for (size_t i = 0; i < submesh.primitiveProperties.size(); ++i) {
        MaterialProperties material = Engine::CreateDefaultMaterial();
        material.textureImageIndices.x = BRDF_LUT_BINDLESS_INDEX;
        Engine::MaterialID matID = materialManager.GetOrCreate(material);

        renderable.primitives[i] = {
            .primitiveIndex = submesh.primitiveProperties[i].index,
            .materialID = matID
        };
    }
    renderable.primitiveCount = submesh.primitiveProperties.size();
    renderable.modelFlags = glm::vec4(0.0f);

    entt::entity entity = state->registry.create();
    state->registry.emplace<Component::RenderableComponent>(entity, renderable);

    Component::TransformComponent transform;
    transform.translation = renderPos;
    transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    transform.scale = renderScale;
    Component::TransformComponent transformComponent = state->registry.emplace<Component::TransformComponent>(entity, transform);
    glm::mat4 initialMatrix = Component::GetMatrix(transformComponent);
    state->registry.emplace<Component::RenderTransformComponent>(entity, initialMatrix, initialMatrix);
    state->registry.emplace<Component::DirtyRenderTransformTag>(entity);

    return entity;
}

entt::entity CreateBox(Core::EngineContext* ctx, Engine::GameState* state, glm::vec3 position, bool bUsePhysics)
{
    ZoneScoped;
    if (!boxHandle.IsValid()) {
        SPDLOG_WARN("[DebugSystem] No box model loaded, press F1 first");
        return entt::null;
    }

    JPH::BodyID bodyId;
    if (bUsePhysics) {
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

        bodyId = bodyInterface.CreateAndAddBody(boxSettings, JPH::EActivation::Activate);
    }


    Render::WillModel* model = ctx->assetManager->GetModel(boxHandle);
    if (!model || model->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
        SPDLOG_WARN("[DebugSystem] Model not ready yet");
        return entt::null;
    }

    Component::RenderableComponent renderable{};
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
    state->registry.emplace<Component::RenderableComponent>(boxEntity, renderable);
    Component::TransformComponent transformComponent = state->registry.emplace<Component::TransformComponent>(boxEntity, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    glm::mat4 initialMatrix = GetMatrix(transformComponent);
    state->registry.emplace<Component::RenderTransformComponent>(boxEntity, initialMatrix, initialMatrix);
    state->registry.emplace<Component::DirtyRenderTransformTag>(boxEntity);
    if (bUsePhysics) {
        state->registry.emplace<Component::PhysicsBodyComponent>(boxEntity, bodyId);
        state->registry.emplace<Component::DynamicPhysicsBodyComponent>(boxEntity, transformComponent.translation, transformComponent.rotation);
        state->registry.emplace<Component::DrawPhysicsDebugComponent>(boxEntity);
    }

    return boxEntity;
}

entt::entity CreateStaticBox(Core::EngineContext* ctx, Engine::GameState* state,
                             JPH::RVec3 physicsPos, JPH::Vec3 halfExtents,
                             glm::vec3 renderPos, glm::vec3 renderScale,
                             glm::vec4 color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f))
{
    ZoneScoped;
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

    Component::RenderableComponent renderable{};
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
    state->registry.emplace<Component::RenderableComponent>(entity, renderable);

    Component::TransformComponent transform;
    transform.translation = renderPos;
    transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    transform.scale = renderScale;
    Component::TransformComponent transformComponent = state->registry.emplace<Component::TransformComponent>(entity, transform);
    state->registry.emplace<Component::PhysicsBodyComponent>(entity, bodyID);
    glm::mat4 initialMatrix = Component::GetMatrix(transformComponent);
    state->registry.emplace<Component::RenderTransformComponent>(entity, initialMatrix, initialMatrix);
    state->registry.emplace<Component::DirtyRenderTransformTag>(entity);

    return entity;
}

// Add this function alongside CreateBox and CreateStaticBox:
entt::entity CreateGlowingBox(Core::EngineContext* ctx, Engine::GameState* state, glm::vec3 position,
                              glm::vec4 emissive = glm::vec4(1.0f, 0.8f, 0.3f, 10.0f), bool bUsePhysics = true)
{
    ZoneScoped;
    if (!boxHandle.IsValid()) {
        SPDLOG_WARN("[DebugSystem] No box model loaded, press F1 first");
        return entt::null;
    }

    JPH::BodyID bodyId;
    if (bUsePhysics) {
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

        bodyId = bodyInterface.CreateAndAddBody(boxSettings, JPH::EActivation::Activate);
    }

    Render::WillModel* model = ctx->assetManager->GetModel(boxHandle);
    if (!model || model->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
        SPDLOG_WARN("[DebugSystem] Model not ready yet");
        return entt::null;
    }

    Component::RenderableComponent renderable{};
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

        material.emissiveFactor = emissive;

        Engine::MaterialID matID = materialManager.GetOrCreate(material);

        renderable.primitives[i] = {
            .primitiveIndex = primitive.index,
            .materialID = matID
        };
    }
    renderable.primitiveCount = submesh.primitiveProperties.size();
    renderable.modelFlags = glm::vec4(0.0f);

    entt::entity boxEntity = state->registry.create();
    state->registry.emplace<Component::RenderableComponent>(boxEntity, renderable);
    Component::TransformComponent& transformComponent = state->registry.emplace<Component::TransformComponent>(boxEntity, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    glm::mat4 initialMatrix = GetMatrix(transformComponent);
    state->registry.emplace<Component::RenderTransformComponent>(boxEntity, initialMatrix, initialMatrix);
    state->registry.emplace<Component::DirtyRenderTransformTag>(boxEntity);

    if (bUsePhysics) {
        state->registry.emplace<Component::PhysicsBodyComponent>(boxEntity, bodyId);
        state->registry.emplace<Component::DynamicPhysicsBodyComponent>(boxEntity, transformComponent.translation, transformComponent.rotation);
    }

    return boxEntity;
}

void DebugUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    if (state->bEnablePhysics) {
        auto view = state->registry.view<Component::MotionBlurMovementComponent, Component::TransformComponent>();
        float time = state->timeFrame->totalTime;
        int index = 0;
        for (auto [entity, motionBlur, transform] : view.each()) {
            float speed = 2.0f + index * 1.5f;
            float offset = sin(time * speed) * 3.0f;

            if (motionBlur.bIsHorizontal) {
                transform.translation.x = 8.0f + offset;
            }
            else {
                transform.translation.y = 10.0f + offset;
            }
            ++index;
            state->registry.emplace_or_replace<Component::DirtyRenderTransformTag>(entity);
        }
    }

    if (state->inputFrame->GetKey(Key::F1).pressed) {
        if (!dragonHandle.IsValid()) {
            dragonHandle = ctx->assetManager->LoadModel("stanford_dragon"_sid);
            //boxHandle = ctx->assetManager->LoadModel("box"_sid);
            boxHandle = ctx->assetManager->LoadModel("4k_box"_sid);
            sphereHandle = ctx->assetManager->LoadModel("sphere"_sid);
            sponzaHandle = ctx->assetManager->LoadModel("sponza"_sid);
            //sponzaHandle = ctx->assetManager->LoadModel("intel_sponza"_sid);
            textureHandle = ctx->assetManager->LoadTexture("smiling_friend"_sid);
            cubemapHandle = ctx->assetManager->LoadCubemap("kloofendal"_sid);
        }

        if (!state->portalPlaneHandle.IsValid()) {
            state->portalPlaneHandle = ctx->assetManager->LoadModel("portal_plane"_sid);
        }
    }

    if (state->inputFrame->GetKey(Key::F2).pressed) {
        Render::WillModel* box = ctx->assetManager->GetModel(boxHandle);
        if (!box || box->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
            SPDLOG_WARN("[DebugSystem] Box model not ready yet");
            return;
        }

        entt::entity floor = CreateStaticBox(
            ctx, state,
            JPH::RVec3(0.0, -0.5, 0.0), JPH::Vec3(10.0f, 0.5f, 10.0f), // physics
            glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f), // render
            glm::vec4(0.5f, 0.5f, 0.5f, 1.0f) // gray
        );
        state->registry.emplace<Component::FloorComponent>(floor);

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
            CreateBox(ctx, state, spawnPos, true);
        }

        SPDLOG_INFO("[DebugSystem] Created physics floor and arena walls");
        SPDLOG_INFO("[DebugSystem] Created falling boxes");

        for (int i = 0; i < 5; ++i) {
            glm::vec3 spawnPos = glm::vec3(8.0f, 10.0f + i * 1.2f, 0.0f);
            entt::entity motionBox = CreateBox(ctx, state, spawnPos, false);
            if (motionBox != entt::null) {
                state->registry.emplace<Component::MotionBlurMovementComponent>(motionBox, true);
            }
        }

        for (int i = 0; i < 5; ++i) {
            glm::vec3 spawnPos = glm::vec3(-8.0f + i * 2.0f, 10.0f, 0);
            entt::entity motionBox = CreateBox(ctx, state, spawnPos, false);
            if (motionBox != entt::null) {
                state->registry.emplace<Component::MotionBlurMovementComponent>(motionBox, false);
            }
        }

        for (int i = 0; i < 5; ++i) {
            glm::vec3 spawnPos = glm::vec3(i * 2.0f - 4.0f, 3.0f, 4.0f);

            glm::vec4 emissive;
            if (i == 0) emissive = glm::vec4(1.0f, 0.2f, 0.1f, 15.0f); // Bright red-orange
            else if (i == 1) emissive = glm::vec4(0.2f, 0.8f, 1.0f, 12.0f); // Bright cyan
            else if (i == 2) emissive = glm::vec4(1.0f, 1.0f, 0.3f, 20.0f); // Super bright yellow
            else if (i == 3) emissive = glm::vec4(0.8f, 0.2f, 1.0f, 10.0f); // Purple
            else emissive = glm::vec4(1.0f, 1.0f, 1.0f, 25.0f); // Mega bright white

            entt::entity glowBox = CreateGlowingBox(ctx, state, spawnPos, emissive, false);
        }

        SPDLOG_INFO("[DebugSystem] Created glowing boxes for bloom testing");

        CreateTextureVisualizer(ctx, state, glm::vec3(0.0f, 5.0f, -15.0f), glm::vec3(5.0f, 5.0f, 1.0f));
        SPDLOG_INFO("[DebugSystem] Created texture visualizer for BRDF LUT");
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
            Component::RenderableComponent renderable{};

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
            state->registry.emplace<Component::RenderableComponent>(dragonEntity, renderable);
            auto& transformComponent = state->registry.emplace<Component::TransformComponent>(dragonEntity, pos, meshRotation, meshScale * 1.5f);
            glm::mat4 initialMatrix = GetMatrix(transformComponent);
            state->registry.emplace<Component::RenderTransformComponent>(dragonEntity, initialMatrix, initialMatrix);
            state->registry.emplace<Component::DirtyRenderTransformTag>(dragonEntity);
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

            Component::RenderableComponent renderable{};
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
            state->registry.emplace<Component::RenderableComponent>(sponzaEntity, renderable);
            auto& transformComponent = state->registry.emplace<Component::TransformComponent>(sponzaEntity, worldTranslations[i], worldRotations[i], worldScales[i]);
            glm::mat4 initialMatrix = GetMatrix(transformComponent);
            state->registry.emplace<Component::RenderTransformComponent>(sponzaEntity, initialMatrix, initialMatrix);
            state->registry.emplace<Component::DirtyRenderTransformTag>(sponzaEntity);
        }

        SPDLOG_INFO("[DebugSystem] Spawned sponza");
    }

    if (state->inputFrame->GetKey(Key::F5).pressed) {
        glm::vec3 posA = glm::vec3(-5.0f, 2.0f, 0.0f);
        glm::quat rotA = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::vec3 posB = glm::vec3(5.0f, 2.0f, 0.0f);
        glm::quat rotB = glm::angleAxis(glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        Component::CreatePortalPair(ctx, state, posA, rotA, posB, rotB);
    }

    if (state->inputFrame->GetKey(Key::F7).pressed) {
        // Floor
        CreateStaticBox(
            ctx, state,
            JPH::RVec3(0.0, -0.5, 0.0), JPH::Vec3(15.0f, 0.5f, 15.0f),
            glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(30.0f, 1.0f, 30.0f),
            glm::vec4(0.4f, 0.4f, 0.4f, 1.0f)
        );

        glm::vec3 posA = glm::vec3(-8.0f, 2.0f, 0.0f);
        glm::quat rotA = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::vec3 posB = glm::vec3(8.0f, 2.0f, 0.0f);
        glm::quat rotB = glm::angleAxis(glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        Component::CreatePortalPair(ctx, state, posA, rotA, posB, rotB);

        // Place colored boxes near each portal tosee through
        CreateGlowingBox(ctx, state, glm::vec3(-6.0f, 1.0f, 0.0f),
                         glm::vec4(1.0f, 0.2f, 0.1f, 10.0f), false);
        CreateGlowingBox(ctx, state, glm::vec3(6.0f, 1.0f, 0.0f),
                         glm::vec4(0.2f, 0.8f, 1.0f, 10.0f), false);

        SPDLOG_INFO("[DebugSystem] Created portal test scene");
    }


    if (state->inputFrame->GetKey(Key::F8).pressed) {
        Render::WillModel* box = ctx->assetManager->GetModel(boxHandle);
        if (!box || box->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
            SPDLOG_WARN("[DebugSystem] Box model not ready yet");
            return;
        }

        const int totalBoxes = 100000;
        // const int totalBoxes = 10000;
        const float boxSpacing = 1.5f;
        const int boxesPerSide = static_cast<int>(cbrt(totalBoxes)) + 1;
        const float gridExtent = static_cast<float>(boxesPerSide) * boxSpacing * 0.5f;
        const float exclusionRadius = 30.0f; // nothing in center area

        int boxesCreated = 0;
        for (int x = 0; x < boxesPerSide && boxesCreated < totalBoxes; x++) {
            for (int y = 0; y < boxesPerSide && boxesCreated < totalBoxes; y++) {
                for (int z = 0; z < boxesPerSide && boxesCreated < totalBoxes; z++) {
                    glm::vec3 spawnPos = glm::vec3(
                        x * boxSpacing - gridExtent,
                        y * boxSpacing - gridExtent,
                        z * boxSpacing - gridExtent
                    );

                    if (abs(spawnPos.x) < exclusionRadius &&
                        abs(spawnPos.y) < exclusionRadius &&
                        abs(spawnPos.z) < exclusionRadius) {
                        continue;
                        }

                    CreateBox(ctx, state, spawnPos, false);
                    boxesCreated++;
                }
            }
        }

        SPDLOG_INFO("Created {} boxes", boxesCreated);
    }
    if (state->inputFrame->GetKey(Key::F9).pressed) {
        Render::WillModel* sphere = ctx->assetManager->GetModel(sphereHandle);
        Render::Cubemap* cubemap = ctx->assetManager->GetCubemap(cubemapHandle);

        if (sphere && sphere->modelLoadState == Render::WillModel::ModelLoadState::Loaded && cubemap && cubemap->loadState == Render::Cubemap::LoadState::Loaded) {
            Component::RenderableComponent renderable{};
            Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();
            Render::MeshInformation& submesh = sphere->modelData.meshes[0];

            for (size_t i = 0; i < submesh.primitiveProperties.size(); ++i) {
                Render::PrimitiveProperty& primitive = submesh.primitiveProperties[i];
                Engine::MaterialID matID = materialManager.GetDefaultMaterial();

                renderable.primitives[i] = {
                    .primitiveIndex = primitive.index,
                    .materialID = matID
                };
            }
            renderable.primitiveCount = submesh.primitiveProperties.size();
            renderable.modelFlags = glm::vec4(0.0f);

            glm::vec3 spherePos = glm::vec3(0.0f, 5.0f, -5.0f);

            entt::entity sphereEntity = state->registry.create();
            state->registry.emplace<Component::RenderableComponent>(sphereEntity, renderable);
            Component::TransformComponent& transform = state->registry.emplace<Component::TransformComponent>(
                sphereEntity, spherePos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(2.0f));
            glm::mat4 initialMatrix = GetMatrix(transform);
            state->registry.emplace<Component::RenderTransformComponent>(sphereEntity, initialMatrix, initialMatrix);
            state->registry.emplace<Component::DirtyRenderTransformTag>(sphereEntity);
            state->registry.emplace<Component::CubemapVisualizeTag>(sphereEntity, cubemap->bindlessHandle);

            SPDLOG_INFO("[DebugSystem] Created cubemap visualization sphere");
        }
        else {
            SPDLOG_WARN("[DebugSystem] Sphere model or cubemap not ready yet");
        }
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

            Component::RenderableComponent renderable{};

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
            state->registry.emplace<Component::RenderableComponent>(dragonEntity, renderable);
            Component::TransformComponent& transformComponent = state->registry.emplace<Component::TransformComponent>(dragonEntity, pos, meshRotation, meshScale * 1.5f);
            glm::mat4 initialMatrix = GetMatrix(transformComponent);
            state->registry.emplace<Component::RenderTransformComponent>(dragonEntity, initialMatrix, initialMatrix);
            state->registry.emplace<Component::DirtyRenderTransformTag>(dragonEntity);
        }

        SPDLOG_INFO("[DebugSystem] Created PCSS test scene: 100x100 floor + vertical dragon column");
    }

    if (state->inputFrame->GetKey(Key::M).pressed) {
        std::filesystem::path musicPath = Platform::GetAssetPath() / "audio/the_entertainer.ogg";
        Core::Handle<Audio::WillAudio> testMusic = ctx->audioManager->LoadAudio("test_music", musicPath);
        ctx->audioManager->PlayMusic(testMusic);
    }
    if (state->inputFrame->GetKey(Key::N).pressed) {
        ctx->audioManager->SetMusicVolume(0.25f);
    }
    if (state->inputFrame->GetKey(Key::B).pressed) {
        ctx->audioManager->SetMusicVolume(1.0f);
    }
}

void DebugProcessPhysicsCollisions(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    std::span<const Physics::DeferredCollisionEvent> events = ctx->physicsSystem->GetCollisionEvents();


    state->registry.clear<Component::AntiGravityComponent>();

    for (const auto& event : events) {
        entt::entity entity1 = state->bodyToEntity.contains(event.body1)
                                   ? state->bodyToEntity[event.body1]
                                   : entt::null;
        entt::entity entity2 = state->bodyToEntity.contains(event.body2)
                                   ? state->bodyToEntity[event.body2]
                                   : entt::null;

        if (entity1 == entt::null || entity2 == entt::null) continue;

        if (state->registry.all_of<Component::FloorComponent>(entity2)) {
            state->registry.emplace_or_replace<Component::AntiGravityComponent>(entity1);
        }
        else if (state->registry.all_of<Component::FloorComponent>(entity1)) {
            state->registry.emplace_or_replace<Component::AntiGravityComponent>(entity2);
        }
    }

    ctx->physicsSystem->ClearCollisionEvents();
    ctx->physicsSystem->ClearActivationEvents();
}

void DebugApplyGroundForces(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    auto view = state->registry.view<Component::AntiGravityComponent, Component::PhysicsBodyComponent>();
    auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();

    for (auto [entity, physics] : view.each()) {
        bodyInterface.AddImpulse(physics.bodyID, JPH::Vec3(0, 100.0f, 0));
    }
}

void DebugRender(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
#ifndef PACKAGED_BUILD
    glm::vec3 debugOffset(-30, 0, 0);

    // Coordinate axes
    frameBuffer->mainViewFamily.debugLines.push_back({
        .start = debugOffset + glm::vec3{0.1f, 0.1f, 0.1f},
        .end = debugOffset + glm::vec3(5, 0, 0) + glm::vec3{0.1f, 0.1f, 0.1f},
        .color = glm::vec4(1, 0, 0, 1)
    });
    frameBuffer->mainViewFamily.debugLines.push_back({
        .start = debugOffset + glm::vec3{0.1f, 0.1f, 0.1f},
        .end = debugOffset + glm::vec3(0, 5, 0) + glm::vec3{0.1f, 0.1f, 0.1f},
        .color = glm::vec4(0, 1, 0, 1)
    });
    frameBuffer->mainViewFamily.debugLines.push_back({
        .start = debugOffset + glm::vec3{0.1f, 0.1f, 0.1f},
        .end = debugOffset + glm::vec3(0, 0, 5) + glm::vec3{0.1f, 0.1f, 0.1f},
        .color = glm::vec4(0, 0, 1, 1)
    });

    // Grid on XZ plane
    for (int i = -5; i <= 5; ++i) {
        frameBuffer->mainViewFamily.debugLines.push_back({
            .start = debugOffset + glm::vec3(i * 2.0f, 0, -10),
            .end = debugOffset + glm::vec3(i * 2.0f, 0, 10),
            .color = glm::vec4(0.3f, 0.3f, 0.3f, 1)
        });
        frameBuffer->mainViewFamily.debugLines.push_back({
            .start = debugOffset + glm::vec3(-10, 0, i * 2.0f),
            .end = debugOffset + glm::vec3(10, 0, i * 2.0f),
            .color = glm::vec4(0.3f, 0.3f, 0.3f, 1)
        });
    }

    // Rotated box
    frameBuffer->mainViewFamily.debugBoxes.push_back({
        .center = debugOffset + glm::vec3(10, 2, 0),
        .extents = glm::vec3(1, 1, 1),
        .rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0)),
        .color = glm::vec4(1, 1, 0, 0.5f)
    });

    // Stack of boxes
    for (int i = 0; i < 3; ++i) {
        frameBuffer->mainViewFamily.debugBoxes.push_back({
            .center = debugOffset + glm::vec3(0, 1.0f + i * 2.5f, 5),
            .extents = glm::vec3(0.8f, 1.0f, 0.8f),
            .rotation = glm::angleAxis(glm::radians(i * 30.0f), glm::vec3(0, 1, 0)),
            .color = glm::vec4(0.2f + i * 0.3f, 0.5f, 1.0f, 0.6f)
        });
    }

    // Sphere at different heights
    for (int i = 0; i < 4; ++i) {
        frameBuffer->mainViewFamily.debugSpheres.push_back({
            .center = debugOffset + glm::vec3(-10, 1.5f + i * 3.0f, 0),
            .radius = 0.5f + i * 0.3f,
            .color = glm::vec4(1, 0.2f * i, 1 - 0.2f * i, 0.5f)
        });
    }

    // Orbital path visualization
    constexpr int orbitSegments = 16;
    glm::vec3 orbitCenter = debugOffset + glm::vec3(0, 5, -5);
    float orbitRadius = 4.0f;
    for (int i = 0; i < orbitSegments; ++i) {
        float angle1 = (float) i / orbitSegments * 2.0f * glm::pi<float>();
        float angle2 = (float) (i + 1) / orbitSegments * 2.0f * glm::pi<float>();

        glm::vec3 p1 = orbitCenter + glm::vec3(glm::cos(angle1) * orbitRadius, 0, glm::sin(angle1) * orbitRadius);
        glm::vec3 p2 = orbitCenter + glm::vec3(glm::cos(angle2) * orbitRadius, 0, glm::sin(angle2) * orbitRadius);

        frameBuffer->mainViewFamily.debugLines.push_back({
            .start = p1,
            .end = p2,
            .color = glm::vec4(0, 1, 1, 1)
        });
    }

    // Bounding volume hierarchy visualization
    frameBuffer->mainViewFamily.debugSpheres.push_back({
        .center = orbitCenter,
        .radius = orbitRadius + 0.5f,
        .color = glm::vec4(1, 0.5f, 0, 0.3f)
    });
#endif

    if (cubemapHandle.IsValid()) {
        frameBuffer->mainViewFamily.skyboxIndex = cubemapHandle.index;
    }
}
} // Game::System
