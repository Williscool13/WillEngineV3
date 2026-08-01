//
// Created by William on 2026-02-08.
//

#ifndef WILL_ENGINE_CORE_COMPONENTS_H
#define WILL_ENGINE_CORE_COMPONENTS_H

#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>

#include <json/nlohmann/json_fwd.hpp>

#include "core/string_id.h"
#include "core/types/transform.h"
#include "engine/component_registry.h"
#include "game/components/component_types.h"

namespace Core
{
struct ViewFamily;
}

namespace Game::Component
{
/**
 * Local transform, relative to parent (or world when no HierarchyComponent).
 */
struct TransformComponent
{
    static constexpr const char* COMPONENT_NAME = "TransformComponent";

    glm::vec3 translation{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};;

    operator Transform() const { return {translation, rotation, scale}; }

    TransformComponent& operator=(const Transform& t)
    {
        translation = t.translation;
        rotation = t.rotation;
        scale = t.scale;
        return *this;
    }

    static void Serialize(const TransformComponent& comp, nlohmann::json& json);

    static void Deserialize(TransformComponent& comp, const nlohmann::json& json);

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

    static void OnConstruct(entt::registry& registry, entt::entity entity);

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

inline glm::mat4 GetMatrix(const TransformComponent& transform)
{
    return glm::translate(glm::mat4(1.0f), transform.translation) * mat4_cast(transform.rotation) * glm::scale(glm::mat4(1.0f), transform.scale);
}

/**
 * Resolved world transform, source of truth for rendering and child composition.
 * Runtime-only, on every transform entity.
 */
struct WorldTransformComponent
{
    glm::vec3 translation{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    operator Transform() const { return {translation, rotation, scale}; }

    WorldTransformComponent& operator=(const Transform& t)
    {
        translation = t.translation;
        rotation = t.rotation;
        scale = t.scale;
        return *this;
    }
};

inline glm::mat4 GetMatrix(const WorldTransformComponent& transform)
{
    return glm::translate(glm::mat4(1.0f), transform.translation) * mat4_cast(transform.rotation) * glm::scale(glm::mat4(1.0f), transform.scale);
}

/**
 * Presence marks an entity as a child. `parentStableId` is the serialized truth, parent is the resolved runtime handle, depth orders the children pass.
 */
struct HierarchyComponent
{
    static constexpr const char* COMPONENT_NAME = "HierarchyComponent";

    entt::entity parent{entt::null};
    StringID parentStableId;
    uint16_t depth{0};

    static void Serialize(const HierarchyComponent& comp, nlohmann::json& json);

    static void Deserialize(HierarchyComponent& comp, const nlohmann::json& json);
};

inline Transform ComposeWorldTransform(const Transform& parent, const Transform& local)
{
    Transform out;
    out.translation = parent.translation + parent.rotation * (parent.scale * local.translation);
    out.rotation = parent.rotation * local.rotation;
    out.scale = parent.scale * local.scale;
    return out;
}

/**
 * Inverse of ComposeWorldTransform: the local transform that places `world` under `parent`. Used to keep world position fixed across a reparent.
 *   Use Case: To get local transform of an entity w.r.t. the cached world transforms
 * @param parent
 * @param world
 * @return
 */
inline Transform ComposeLocalFromWorld(const Transform& parent, const Transform& world)
{
    const glm::quat invParentRotation = glm::inverse(parent.rotation);
    Transform local;
    local.translation = (invParentRotation * (world.translation - parent.translation)) / parent.scale;
    local.rotation = glm::normalize(invParentRotation * world.rotation);
    local.scale = world.scale / parent.scale;
    return local;
}

/**
 * Expensive.
 * Walks the parent chain to resolve an entity's world transform from current local values, independent of the per-frame resolve pass.
 * @param registry
 * @param entity
 * @return
 */
Transform ComputeWorldTransform(const entt::registry& registry, entt::entity entity);

struct DirtyTransformTag
{};
}


#endif //WILL_ENGINE_CORE_COMPONENTS_H
