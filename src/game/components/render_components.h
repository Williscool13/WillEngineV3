//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_RENDER_COMPONENTS_H
#define WILL_ENGINE_RENDER_COMPONENTS_H

#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>

#include "core/containers/inline_vector.h"
#include "engine/material_manager.h"
#include "engine/asset_manager_types.h"
#include "engine/resources/model/model_types.h"
#include "engine/resources/model/mesh_primitive_store.h"

namespace Game::Component
{
struct MultiframeDirtyTransformComponent
{
    // 2 to update Prev next frame.
    int32_t counter{2};
};

struct RenderTransformComponent
{
    glm::mat4 modelMatrix;
    glm::mat4 previousMatrix;
    glm::vec3 renderOffset{0.0f};
    glm::quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

// Quad transform for an area light's emissive surface. Spawned off AreaLightComponent (lifetime-linked), rolled each frame in RenderPrepareTransforms.
struct AreaLightTransformComponent
{
    glm::mat4 modelMatrix{1.0f};
    glm::mat4 previousMatrix{1.0f};
};

// Sphere transform for a sphere light's emissive surface. Spawned off SphereLightComponent (lifetime-linked), rolled each frame in RenderPrepareTransforms.
struct SphereLightTransformComponent
{
    glm::mat4 modelMatrix{1.0f};
    glm::mat4 previousMatrix{1.0f};
};

struct MeshRuntime
{
    /**
     * Entity's contiguous run in EngineState::meshPrimitiveStore
     */
    Engine::MeshPrimitiveStore::Range range{};
    Engine::StaticModelHandle modelHandle{};
    bool visible{true};
    bool ddgiVisible{true};

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};
}

#endif //WILL_ENGINE_RENDER_COMPONENTS_H
