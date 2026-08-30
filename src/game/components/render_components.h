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
#include "game/components/common/stable_id_component.h"
#include "engine/material_manager.h"
#include "engine/asset_manager_types.h"
#include "engine/resources/model/model_types.h"
#include "engine/resources/model/instance_store.h"
#include "engine/resources/model/model_store.h"
#include "render/interface/render_interface.h"

namespace Engine
{
struct EngineState;
}

namespace Game::Component
{
struct MultiframeDirtyComponent
{
    // FRAME_BUFFER_COUNT to update all 3 host buffers. +1 for prevModeLmatrix
    int32_t counter{Core::FRAME_BUFFER_COUNT + 1};
};

struct RenderFlagsComponent
{
    static constexpr const char* COMPONENT_NAME = "RenderFlagsComponent";

    static constexpr uint32_t VISIBLE = 1u << 0;
    static constexpr uint32_t PROBE_BAKE_INCLUDE = 1u << 1;
    static constexpr uint32_t DDGI_CONTRIBUTE = 1u << 2;
    static constexpr uint32_t MOTION_BLUR = 1u << 3;
    static constexpr uint32_t ALPHA_CUTOUT = 1u << 4;
    static constexpr uint32_t EMISSIVE_LIGHT = 1u << 5;
    static constexpr uint32_t DEFAULT_FLAGS = VISIBLE | PROBE_BAKE_INCLUDE | DDGI_CONTRIBUTE | MOTION_BLUR | ALPHA_CUTOUT;

    uint32_t flags{DEFAULT_FLAGS};

    [[nodiscard]] bool Has(uint32_t bit) const { return (flags & bit) != 0; }

    static void Serialize(const RenderFlagsComponent& comp, Engine::TextWriter& w);
    static void Deserialize(RenderFlagsComponent& comp, const Engine::TextReader& r);
};

void SetRenderFlag(Engine::EngineState* state, entt::entity entity, RenderFlagsComponent& renderFlags, uint32_t bit, bool value);

struct RenderTransformComponent
{
    glm::mat4 modelMatrix;
    glm::mat4 previousMatrix;
    glm::vec3 renderOffset{0.0f};
    glm::quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct MeshRuntime
{
    /**
     * Entity's contiguous run in EngineState::instanceStore
     */
    Engine::InstanceStore::Range range{};
    /**
     * Entity's model matrix slots in EngineState::modelStore
     */
    Engine::ModelStore::Range modelRange{};
    Engine::StaticModelHandle modelHandle{};

    uint64_t stableId{StableIdComponent::NO_ID};

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
};
}

#endif //WILL_ENGINE_RENDER_COMPONENTS_H
