//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_LAYER_INTERFACE_H
#define WILL_ENGINE_LAYER_INTERFACE_H

#include <Jolt/Jolt.h>

#include "JoltPhysics/Jolt/Physics/Collision/ObjectLayer.h"
#include "JoltPhysics/Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h"

namespace Physics
{
namespace Layers
{
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer PLAYER = 2;
    static constexpr JPH::ObjectLayer SENSOR = 3;
    static constexpr JPH::ObjectLayer DEBRIS = 4;
    static constexpr JPH::ObjectLayer IGNORE_DEBRIS = 5;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 6;

    /** For layer masks: a query given Bit(PLAYER) | Bit(SENSOR) skips both. */
    constexpr uint32_t Bit(JPH::ObjectLayer layer) { return 1u << layer; }
}

namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint NUM_LAYERS = 2;
}
} // Physics

#endif //WILL_ENGINE_LAYER_INTERFACE_H