//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_LAYER_INTERFACE_H
#define WILL_ENGINE_LAYER_INTERFACE_H

#include <Jolt/Jolt.h>

#include "JoltPhysics/Jolt/Physics/Collision/ObjectLayer.h"
#include "JoltPhysics/Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h"
#include "physics/physics_layers.h"

namespace Physics
{
static_assert(sizeof(JPH::ObjectLayer) == sizeof(uint16_t), "Layers are uint16_t object layers");

namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint NUM_LAYERS = 2;
}
} // Physics

#endif //WILL_ENGINE_LAYER_INTERFACE_H
