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
    return Transform{transform.translation, transform.rotation, transform.scale}.GetMatrix();
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
    return Transform{transform.translation, transform.rotation, transform.scale}.GetMatrix();
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
    // SSE lanes are [x, y, z, w]; loads are member-wise so glm quat storage order is irrelevant
    const __m128 pq = _mm_set_ps(parent.rotation.w, parent.rotation.z, parent.rotation.y, parent.rotation.x);
    const __m128 lq = _mm_set_ps(local.rotation.w, local.rotation.z, local.rotation.y, local.rotation.x);

    // Hamilton product parent.rotation * local.rotation
    const __m128 signW = _mm_set_ps(-0.0f, 0.0f, 0.0f, 0.0f);
    __m128 rq = _mm_mul_ps(_mm_shuffle_ps(pq, pq, _MM_SHUFFLE(3, 3, 3, 3)), lq);
    rq = _mm_add_ps(rq, _mm_xor_ps(signW, _mm_mul_ps(_mm_shuffle_ps(pq, pq, _MM_SHUFFLE(0, 2, 1, 0)), _mm_shuffle_ps(lq, lq, _MM_SHUFFLE(0, 3, 3, 3)))));
    rq = _mm_add_ps(rq, _mm_xor_ps(signW, _mm_mul_ps(_mm_shuffle_ps(pq, pq, _MM_SHUFFLE(1, 0, 2, 1)), _mm_shuffle_ps(lq, lq, _MM_SHUFFLE(1, 1, 0, 2)))));
    rq = _mm_sub_ps(rq, _mm_mul_ps(_mm_shuffle_ps(pq, pq, _MM_SHUFFLE(2, 1, 0, 2)), _mm_shuffle_ps(lq, lq, _MM_SHUFFLE(2, 0, 2, 1))));

    // Rotate the scaled local translation: v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
    const __m128 v = _mm_set_ps(0.0f, parent.scale.z * local.translation.z, parent.scale.y * local.translation.y, parent.scale.x * local.translation.x);
    auto cross3 = [](__m128 a, __m128 b) {
        const __m128 aYzx = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 bYzx = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 c = _mm_sub_ps(_mm_mul_ps(a, bYzx), _mm_mul_ps(aYzx, b));
        return _mm_shuffle_ps(c, c, _MM_SHUFFLE(3, 0, 2, 1));
    };
    const __m128 t = _mm_add_ps(cross3(pq, v), _mm_mul_ps(_mm_shuffle_ps(pq, pq, _MM_SHUFFLE(3, 3, 3, 3)), v));
    const __m128 c2 = cross3(pq, t);
    const __m128 rotated = _mm_add_ps(v, _mm_add_ps(c2, c2));

    alignas(16) float outQ[4];
    alignas(16) float outV[4];
    _mm_store_ps(outQ, rq);
    _mm_store_ps(outV, rotated);

    Transform out;
    out.translation = glm::vec3(parent.translation.x + outV[0], parent.translation.y + outV[1], parent.translation.z + outV[2]);
    out.rotation = glm::quat(outQ[3], outQ[0], outQ[1], outQ[2]);
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
