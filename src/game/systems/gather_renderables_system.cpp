//
// Created by William on 2025-12-26.
//

#include "gather_renderables_system.h"

#include "../components/render/portal_plane_component.h"
#include "core/include/engine_context.h"
#include "engine/engine_api.h"
#include "game/components/renderable_component.h"
#include "game/components/transform_component.h"
#include "game/components/physics/dynamic_physics_body_component.h"


namespace Game::System
{
void GatherRenderables(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    auto& materialManager = ctx->assetManager->GetMaterialManager();

    // Gather regular renderables
    {
        const auto view = state->registry.view<RenderableComponent, TransformComponent>(entt::exclude<PortalPlaneComponent>);

        for (const auto& [entity, renderable, transform] : view.each()) {
            glm::mat4 currentMatrix;

            if (auto* physics = state->registry.try_get<DynamicPhysicsBodyComponent>(entity)) {
                float alpha = state->physicsInterpolationAlpha;
                glm::vec3 interpPos = glm::mix(physics->previousPosition, transform.translation, alpha);
                glm::quat interpRot = glm::slerp(physics->previousRotation, transform.rotation, alpha);
                currentMatrix = glm::translate(glm::mat4(1.0f), interpPos) * glm::mat4_cast(interpRot);
            }
            else {
                currentMatrix = GetMatrix(transform);
            }

            frameBuffer->mainViewFamily.modelMatrices.push_back({currentMatrix, renderable.previousModelMatrix});
            uint32_t modelIndex = frameBuffer->mainViewFamily.modelMatrices.size() - 1;

            for (uint8_t i = 0; i < renderable.primitiveCount; ++i) {
                auto& prim = renderable.primitives[i];
                frameBuffer->mainViewFamily.mainInstances.push_back({
                    .primitiveIndex = prim.primitiveIndex,
                    .materialID = prim.materialID,
                    .modelIndex = modelIndex
                });
            }

            renderable.previousModelMatrix = currentMatrix;
        }
    }

    // Gather portal planes (custom stencil draws with stencil=1)
    {
        const auto portalView = state->registry.view<PortalPlaneComponent, RenderableComponent, TransformComponent>();

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

            for (const auto& [entity, renderable, transform] : portalView.each()) {
                glm::mat4 currentMatrix = GetMatrix(transform);

                frameBuffer->mainViewFamily.modelMatrices.push_back({currentMatrix, renderable.previousModelMatrix});
                uint32_t modelIndex = frameBuffer->mainViewFamily.modelMatrices.size() - 1;

                for (uint8_t i = 0; i < renderable.primitiveCount; ++i) {
                    auto& prim = renderable.primitives[i];
                    portalBatch->instances.push_back({
                        .primitiveIndex = prim.primitiveIndex,
                        .materialID = prim.materialID,
                        .modelIndex = modelIndex
                    });
                }

                renderable.previousModelMatrix = currentMatrix;
            }
        }
    }

    std::unordered_map<Engine::MaterialID, uint32_t> materialRemap;
    for (auto& instance : frameBuffer->mainViewFamily.mainInstances) {
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
