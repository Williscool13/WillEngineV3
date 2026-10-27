//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_SPLINE_MESH_COMPONENT_H
#define WILL_ENGINE_SPLINE_MESH_COMPONENT_H

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include "procedural_mesh_component.h"
#include "engine/core/registry.h"
#include "engine/asset_manager_types.h"
#include "engine/core/material_id.h"
#include "engine/spline/spline.h"
#include "engine/component_registry.h"
#include "engine/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Engine::Component
{
struct SplineMeshComponent
{
    static constexpr const char* COMPONENT_NAME = "SplineMeshComponent";

    Engine::Spline spline;
    float radius{0.5f};
    float rollAngle{0.0f};
    int32_t sides{8};
    int32_t segmentsPerSpan{8};
    bool bCaps{true};
    bool bCrossPlanks{false};
    int32_t crossPlankInterval{4};
    float crossPlankHeight{0.0f};
    float crossPlankThickness{0.1f};
    float crossPlankLength{0.3f};
    Engine::SplineProfile profile{};
    Engine::SplineRailing railing{};
    float tubeThickness{0.0f};
    bool bFacetNormals{false};

    Engine::MaterialID material{};
    glm::vec3 renderOffset{0.0f};

    WILL_REFLECT(SplineMeshComponent,
        WILL_FIELD(spline),
        WILL_FIELD(radius),
        WILL_FIELD(rollAngle),
        WILL_FIELD(sides),
        WILL_FIELD(segmentsPerSpan),
        WILL_FIELD(bCaps),
        WILL_FIELD(bCrossPlanks),
        WILL_FIELD(crossPlankInterval),
        WILL_FIELD(crossPlankHeight),
        WILL_FIELD(crossPlankThickness),
        WILL_FIELD(crossPlankLength),
        WILL_FIELD(profile),
        WILL_FIELD(railing),
        WILL_FIELD(material),
        WILL_FIELD(renderOffset),
        WILL_FIELD(tubeThickness),
        WILL_FIELD(bFacetNormals))

    static void Sanitize(SplineMeshComponent& comp);
    static bool CanAdd(const Engine::SystemRegistry& registry, entt::entity entity);
    static void OnConstruct(Engine::SystemRegistry& registry, entt::entity entity);
    static void OnDestroy(Engine::SystemRegistry& registry, entt::entity entity);
    static void OnEditCommit(Engine::SystemRegistry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};

/** Generation requested; StartSplineMeshLoads kicks the model build. */
struct SplineMeshLoadRequest
{
    static constexpr const char* COMPONENT_NAME = "SplineMeshLoadRequest";
};

/** Model build in flight; ResolveSplineMeshLoads binds it once finished. */
struct SplineMeshLoadingFlag
{
    static constexpr const char* COMPONENT_NAME = "SplineMeshLoadingFlag";
};

Engine::SplineParams ToSplineParams(const SplineMeshComponent& component);
}

#endif //WILL_ENGINE_SPLINE_MESH_COMPONENT_H
