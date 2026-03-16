//
// Created by William on 2026-03-04.
//

#ifndef WILL_ENGINE_COMPONENT_COPY_H
#define WILL_ENGINE_COMPONENT_COPY_H

#include <entt/entt.hpp>

#include "../components/common_components.h"
#include "../components/render_components.h"

namespace Game
{
// Base: raw copy of all fields.
// Specialize to strip transient data before OnComponentAdded is called.
template<typename T>
T CopyComponent(const T& src, entt::registry& dstReg) { return src; }

template<> Component::StableIdComponent CopyComponent(const Component::StableIdComponent& src, entt::registry& dstReg);
template<> Component::StaticMeshComponent CopyComponent(const Component::StaticMeshComponent& src, entt::registry& dstReg);
template<> Component::ProceduralMeshComponent CopyComponent(const Component::ProceduralMeshComponent& src, entt::registry& dstReg);
template<> Component::SplineMeshComponent CopyComponent(const Component::SplineMeshComponent& src, entt::registry& dstReg);
} // Game

#endif //WILL_ENGINE_COMPONENT_COPY_H
