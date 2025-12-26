//
// Created by William on 2025-12-26.
//

#include "gather_renderables_component.h"

#include "engine/engine_api.h"
#include "game/components/renderable_component.h"
#include "game/components/transform_component.h"
#include "game/components/physics/physics_body_component.h"
#include "render/frame_resources.h"
#include "render/shaders/model_interop.h"


namespace Game::System
{
void GatherRenderables(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer, const Render::FrameResources* frameResources)
{
    auto* instanceBuffer = static_cast<Instance*>(frameResources->instanceBuffer.allocationInfo.pMappedData);
    auto modelBuffer = static_cast<Model*>(frameResources->modelBuffer.allocationInfo.pMappedData);
    auto materialBuffer = static_cast<MaterialProperties*>(frameResources->materialBuffer.allocationInfo.pMappedData);

    const auto view = state->registry.view<RenderableComponent, TransformComponent>();

    for (const auto& [entity, renderable, transform] : view.each()) {
        glm::mat4 currentMatrix;

        if (auto* physics = state->registry.try_get<PhysicsBodyComponent>(entity)) {
            float alpha = state->physicsInterpolationAlpha;
            glm::vec3 interpPos = glm::mix(physics->previousPosition, transform.translation, alpha);
            glm::quat interpRot = glm::slerp(physics->previousRotation, transform.rotation, alpha);

            currentMatrix = glm::translate(glm::mat4(1.0f), interpPos) * glm::mat4_cast(interpRot);
        }
        else {
            currentMatrix = GetMatrix(transform);
        }

        modelBuffer[renderable.modelEntry.index] = {
            currentMatrix, // current frame
            currentMatrix, // previous frame todo: add later
            renderable.modelFlags
        };
        instanceBuffer[renderable.instanceEntry.index] = renderable.instance;
        materialBuffer[renderable.materialEntry.index] = renderable.material;

        frameBuffer->mainViewFamily.instances.push_back(renderable.instanceEntry);
    }
}
}
