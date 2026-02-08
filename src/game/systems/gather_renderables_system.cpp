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

    auto view = state->registry.view<Component::TransformComponent, Component::RenderTransformComponent, Component::DirtyRenderTransformTag>();

    constexpr size_t TASK_THRESHOLD = 1000;
    if (view.size_hint() < TASK_THRESHOLD) {
        ZoneScopedN("Serial");
        for (auto [entity, transform, renderTransform] : view.each()) {
            renderTransform.previousMatrix = renderTransform.modelMatrix;
            renderTransform.modelMatrix = GetMatrix(transform);
        }
    }
    else {
        ZoneScopedN("Parallel");
        std::vector entities(view.begin(), view.end());

        enki::TaskSet task(entities.size(), [&](enki::TaskSetPartition range, uint32_t) {
            for (uint32_t i = range.start; i < range.end; ++i) {
                auto entity = entities[i];
                auto& transform = view.get<Component::TransformComponent>(entity);
                auto& renderTransform = view.get<Component::RenderTransformComponent>(entity);

                renderTransform.previousMatrix = renderTransform.modelMatrix;
                renderTransform.modelMatrix = GetMatrix(transform);
            }
        });
        ctx->scheduler->AddTaskSetToPipe(&task);
        ctx->scheduler->WaitforTask(&task);
    }

    state->registry.clear<Component::DirtyRenderTransformTag>();
}

void GatherRenderables(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    auto& materialManager = ctx->assetManager->GetMaterialManager();

    // Gather regular renderables
    {
        ZoneScopedN("MainSceneRenderables");
        auto view = state->registry.view<Component::RenderableComponent, Component::RenderTransformComponent>(
            entt::exclude<Component::PortalPlaneComponent>);

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
            Core::CustomStencilDrawBatch* portalBatch = nullptr;
            for (auto& draw : frameBuffer->mainViewFamily.customStencilDraws) {
                if (draw.stencilValue == 1) {
                    portalBatch = &draw;
                    break;
                }
            }

            if (!portalBatch) {
                frameBuffer->mainViewFamily.customStencilDraws.push_back({.stencilValue = 1});
                portalBatch = &frameBuffer->mainViewFamily.customStencilDraws.back();
            }

            for (auto [entity, renderable, renderTransform] : portalView.each()) {
                auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.size());
                frameBuffer->mainViewFamily.modelMatrices.push_back({renderTransform.modelMatrix, renderTransform.previousMatrix});

                for (uint8_t i = 0; i < renderable.primitiveCount; ++i) {
                    auto& prim = renderable.primitives[i];
                    portalBatch->instances.push_back({
                        .primitiveIndex = prim.primitiveIndex,
                        .materialID = prim.materialID,
                        .modelIndex = modelIndex
                    });
                }
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

        for (auto& customDraw : frameBuffer->mainViewFamily.customStencilDraws) {
            for (auto& instance : customDraw.instances) {
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
