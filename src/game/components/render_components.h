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
#include "engine/resources/model/instance_store.h"
#include "engine/resources/model/model_store.h"

namespace Game::Component
{
struct MultiframeDirtyTransformComponent
{
    // 2 to update Prev next frame.
    int32_t counter{2};
};

struct RenderFlagsComponent
{
    static constexpr const char* COMPONENT_NAME = "RenderFlagsComponent";

    static constexpr uint32_t VISIBLE = 1u << 0;
    static constexpr uint32_t PROBE_BAKE_INCLUDE = 1u << 1;
    static constexpr uint32_t DDGI_CONTRIBUTE = 1u << 2;
    static constexpr uint32_t MOTION_BLUR = 1u << 3;
    static constexpr uint32_t ALPHA_CUTOUT = 1u << 4;
    static constexpr uint32_t DEFAULT_FLAGS = VISIBLE | PROBE_BAKE_INCLUDE | DDGI_CONTRIBUTE | MOTION_BLUR | ALPHA_CUTOUT;

    uint32_t flags{DEFAULT_FLAGS};

    [[nodiscard]] bool Has(uint32_t bit) const { return (flags & bit) != 0; }

    void Set(uint32_t bit, bool value)
    {
        if (value) { flags |= bit; }
        else { flags &= ~bit; }
    }

    static void Serialize(const RenderFlagsComponent& comp, Engine::TextWriter& w);
    static void Deserialize(RenderFlagsComponent& comp, const Engine::TextReader& r);
};

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
    bool visible{true};
    bool ddgiVisible{true};
    bool motionBlur{true};
    bool alphaCutout{true};

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};
}

#endif //WILL_ENGINE_RENDER_COMPONENTS_H
