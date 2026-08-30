//
// Created by William on 2026-08-30.
//

#ifndef WILL_ENGINE_MESH_SOURCE_EXCLUSION_H
#define WILL_ENGINE_MESH_SOURCE_EXCLUSION_H

#include <type_traits>

#include <entt/entt.hpp>

#include "game/components/render/module_mesh_component.h"
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/static_mesh_primitive_component.h"
#include "game/components/render/text3d_component.h"

namespace Game::Component
{
template<typename... Ts>
struct MeshSourceSet
{
    template<typename Self>
    static bool NoneOtherThan(const entt::registry& registry, entt::entity entity)
    {
        static_assert((std::is_same_v<Ts, Self> || ...), "Self must be one of the mesh sources");
        return (... && (std::is_same_v<Ts, Self> || !registry.all_of<Ts>(entity)));
    }
};

/** An entity may hold at most one of these. */
using MeshSources = MeshSourceSet<StaticMeshComponent, StaticMeshPrimitiveComponent, ProceduralMeshComponent, SplineMeshComponent, Text3DComponent, ModuleMeshComponent>;
}

#endif //WILL_ENGINE_MESH_SOURCE_EXCLUSION_H
