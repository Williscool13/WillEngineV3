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
#include "engine/components/common/stable_id_component.h"
#include "engine/material_manager.h"
#include "engine/asset_manager_types.h"
#include "engine/resources/model/model_types.h"
#include "engine/resources/model/instance_store.h"
#include "engine/resources/model/model_store.h"
#include "engine/reflection/reflection.h"
#include "render/interface/render_interface.h"

namespace Engine
{
struct EngineState;
}

namespace Engine::Component
{
struct MultiframeDirtyComponent
{
    // FRAME_BUFFER_COUNT to update all 3 host buffers. +1 for prevModeLmatrix
    int32_t counter{Core::FRAME_BUFFER_COUNT + 1};
};

struct RenderFlagsComponent
{
    static constexpr const char* COMPONENT_NAME = "RenderFlagsComponent";

    bool bVisible{true};
    bool bProbeBakeInclude{true};
    bool bDdgiContribute{true};
    bool bMotionBlur{true};
    bool bAlphaCutout{true};
    bool bEmissiveLight{false};
    bool bCameraMotionBlur{true};

    WILL_REFLECT(RenderFlagsComponent,
        WILL_FIELD(bVisible, .key = "visible"),
        WILL_FIELD(bProbeBakeInclude, .key = "probeBake"),
        WILL_FIELD(bDdgiContribute, .key = "ddgi"),
        WILL_FIELD(bMotionBlur, .key = "motionBlur"),
        WILL_FIELD(bAlphaCutout, .key = "alphaCutout"),
        WILL_FIELD(bEmissiveLight, .key = "emissiveLight"),
        WILL_FIELD(bCameraMotionBlur, .key = "cameraMotionBlur"))

    static void OnEditPreview(entt::registry& registry, entt::entity entity);
    static void OnEditCommit(entt::registry& registry, entt::entity entity);
};

enum RenderFlagToggle : uint32_t
{
    RENDER_TOGGLE_VISIBLE = 1u << 0,
    RENDER_TOGGLE_DDGI = 1u << 1,
    RENDER_TOGGLE_PROBE_BAKE = 1u << 2,
    RENDER_TOGGLE_MOTION_BLUR = 1u << 3,
    RENDER_TOGGLE_CAMERA_MOTION_BLUR = 1u << 4,
    RENDER_TOGGLE_ALPHA_CUTOUT = 1u << 5,
    RENDER_TOGGLE_EMISSIVE = 1u << 6,
    RENDER_TOGGLE_ALL = 0x7Fu,
};

/**
 * @param toggles RENDER_TOGGLE_* bits
 * @return true when Emissive Light changed; the caller's mesh must rebuild its emissive light table
 */
bool DrawRenderFlagToggles(Engine::EditContext& edit, uint32_t toggles);

struct RenderTransformComponent
{
    static constexpr const char* COMPONENT_NAME = "RenderTransformComponent";

    glm::mat4 modelMatrix;
    glm::mat4 previousMatrix;
    glm::vec3 renderOffset{0.0f};
    glm::quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct MeshRuntime
{
    static constexpr const char* COMPONENT_NAME = "MeshRuntime";

    /**
     * Entity's contiguous run in EngineState::instanceStore
     */
    Engine::InstanceStore::Range range{};
    /**
     * Entity's model matrix slots in EngineState::modelStore
     */
    Engine::ModelStore::Range modelRange{};
    Engine::StaticModelHandle modelHandle{};
    Engine::StaticModelHandle pendingModelHandle{};

    uint64_t stableId{StableIdComponent::NO_ID};

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
};
}

#endif //WILL_ENGINE_RENDER_COMPONENTS_H
