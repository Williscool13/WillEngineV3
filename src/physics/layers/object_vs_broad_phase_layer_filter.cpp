//
// Created by William on 2025-12-25.
//

#include "object_vs_broad_phase_layer_filter.h"

#include "layer_interface.h"

namespace Physics
{
bool ObjectVsBroadPhaseLayerFilterImpl::ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const
{
    return !(inLayer1 == Layers::STATIC && inLayer2 == BroadPhaseLayers::STATIC);
}
} // Physics