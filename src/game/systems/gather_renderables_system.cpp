//
// Created by William on 2025-12-26.
//

#include "gather_renderables_system.h"

#include <tracy/Tracy.hpp>

#include "core/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/fwd_components.h"


namespace Game::System
{
void UpdateRenderTransforms(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;

    auto dirtyView = state->registry.view<Component::TransformComponent, Component::RenderTransformComponent,
        Component::DirtyRenderTransformTag>(entt::exclude<Component::DynamicPhysicsBodyComponent>);
    constexpr size_t TASK_THRESHOLD = 1000;
    if (dirtyView.size_hint() < TASK_THRESHOLD) {
        ZoneScopedN("Serial");
        for (auto [entity, transform, renderTransform] : dirtyView.each()) {
            renderTransform.previousMatrix = renderTransform.modelMatrix;
            renderTransform.modelMatrix = GetMatrix(transform);
        }
    }
    else {
        ZoneScopedN("Parallel");
        std::vector entities(dirtyView.begin(), dirtyView.end());

        enki::TaskSet task(entities.size(), [&](enki::TaskSetPartition range, uint32_t) {
            for (uint32_t i = range.start; i < range.end; ++i) {
                auto entity = entities[i];
                auto& transform = dirtyView.get<Component::TransformComponent>(entity);
                auto& renderTransform = dirtyView.get<Component::RenderTransformComponent>(entity);

                renderTransform.previousMatrix = renderTransform.modelMatrix;
                renderTransform.modelMatrix = GetMatrix(transform);
            }
        });
        ctx->scheduler->AddTaskSetToPipe(&task);
        ctx->scheduler->WaitforTask(&task);
    }

    state->registry.clear<Component::DirtyRenderTransformTag>();

    // Physics always dirty until I find a better way
    auto physicsView = state->registry.view<Component::DynamicPhysicsBodyComponent, Component::TransformComponent, Component::RenderTransformComponent>();
    for (auto [entity, physics, transform, renderTransform] : physicsView.each()) {
        renderTransform.previousMatrix = renderTransform.modelMatrix;

        float alpha = state->physicsInterpolationAlpha;
        glm::vec3 interpPos = glm::mix(physics.previousPosition, transform.translation, alpha);
        glm::quat interpRot = glm::slerp(physics.previousRotation, transform.rotation, alpha);
        renderTransform.modelMatrix = glm::translate(glm::mat4(1.0f), interpPos) * glm::mat4_cast(interpRot);
    }
}

void GatherRenderables(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    auto& materialManager = ctx->assetManager->GetMaterialManager();

    // Gather regular renderables
    {
        ZoneScopedN("MainSceneRenderables");
        auto view = state->registry.view<Component::RenderableComponent, Component::RenderTransformComponent>(
            entt::exclude<Component::PortalPlaneComponent, Component::CubemapVisualizeTag>);

        for (auto [entity, renderable, renderTransform] : view.each()) {
            auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.size());
            frameBuffer->mainViewFamily.modelMatrices.push_back({renderTransform.modelMatrix, renderTransform.previousMatrix});

            for (uint8_t i = 0; i < renderable.primitiveCount; ++i) {
                auto& prim = renderable.primitives[i];
                frameBuffer->mainViewFamily.mainPassInstances.push_back({
                    .primitiveIndex = prim.primitiveIndex,
                    .materialID = prim.materialID,
                    .modelIndex = modelIndex
                });
            }
        }
    }

    // Gather portal planes
    {
        ZoneScopedN("PortalRenderables");
        auto portalView = state->registry.view<Component::PortalPlaneComponent, Component::RenderableComponent, Component::RenderTransformComponent>();

        if (portalView.size_hint() > 0) {
            auto& portalDraw = frameBuffer->mainViewFamily.customShaderDraws["portal_rendering"];
            if (portalDraw.instances.empty()) {
                portalDraw.pipelineName = "portal_rendering";
                portalDraw.pushConstantData = {}; // no custom data
                portalDraw.instanceBufferName = "portal_instance_buffer";
                portalDraw.stencilValue = 1;
            }

            for (auto [entity, renderable, renderTransform] : portalView.each()) {
                auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.size());
                frameBuffer->mainViewFamily.modelMatrices.push_back({renderTransform.modelMatrix, renderTransform.previousMatrix});

                for (uint8_t i = 0; i < renderable.primitiveCount; ++i) {
                    auto& prim = renderable.primitives[i];
                    portalDraw.instances.push_back({
                        .primitiveIndex = prim.primitiveIndex,
                        .materialID = prim.materialID,
                        .modelIndex = modelIndex
                    });
                }
            }
        }
    }

    // Gather cubemap visualizations
    {
        ZoneScopedN("CubemapVisualizations");
        auto cubemapView = state->registry.view<Component::CubemapVisualizeTag, Component::RenderableComponent, Component::RenderTransformComponent>();

        for (auto [entity, renderable, renderTransform] : cubemapView.each()) {
            auto& cubemapVis = frameBuffer->mainViewFamily.customShaderDraws["cubemap_visualize"];
            if (cubemapVis.instances.empty()) {
                cubemapVis.pipelineName = "cubemap_visualize";
                cubemapVis.pushConstantData = {
                    0,
                    ASSET_SAMPLER_BINDLESS_INDEX,
                    0
                };
                cubemapVis.instanceBufferName = "cubemap_visualize_instance_buffer";
            }

            auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.size());
            frameBuffer->mainViewFamily.modelMatrices.push_back({renderTransform.modelMatrix, renderTransform.previousMatrix});

            for (uint8_t i = 0; i < renderable.primitiveCount; ++i) {
                auto& prim = renderable.primitives[i];
                cubemapVis.instances.push_back({
                    .primitiveIndex = prim.primitiveIndex,
                    .materialID = prim.materialID,
                    .modelIndex = modelIndex
                });
            }
        }
    }

    // Material remap
    {
        ZoneScopedN("Material Remap");
        std::unordered_map<Engine::MaterialID, uint32_t> materialRemap;
        for (auto& instance : frameBuffer->mainViewFamily.mainPassInstances) {
            if (!materialRemap.contains(instance.materialID)) {
                uint32_t gpuIndex = frameBuffer->mainViewFamily.materials.size();
                materialRemap[instance.materialID] = gpuIndex;
                frameBuffer->mainViewFamily.materials.push_back(materialManager.Get(instance.materialID));
            }
            instance.gpuMaterialIndex = materialRemap[instance.materialID];
        }

        for (auto& customDraw : frameBuffer->mainViewFamily.customShaderDraws) {
            for (auto& instance : customDraw.second.instances) {
                if (!materialRemap.contains(instance.materialID)) {
                    uint32_t gpuIndex = frameBuffer->mainViewFamily.materials.size();
                    materialRemap[instance.materialID] = gpuIndex;
                    frameBuffer->mainViewFamily.materials.push_back(materialManager.Get(instance.materialID));
                }
                instance.gpuMaterialIndex = materialRemap[instance.materialID];
            }
        }
    }
}
}
