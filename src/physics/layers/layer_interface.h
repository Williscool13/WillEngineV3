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
    static constexpr JPH::ObjectLayer STATIC = 0;
    static constexpr JPH::ObjectLayer DYNAMIC = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer STATIC(0);
    static constexpr JPH::BroadPhaseLayer DYNAMIC(1);
    static constexpr JPH::uint NUM_LAYERS = 2;
}
} // Physics

#endif //WILL_ENGINE_LAYER_INTERFACE_H