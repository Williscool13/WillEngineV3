//
// Created by William on 2025-12-25.
//

#include "object_layer_pair_filter.h"

#include "layer_interface.h"

namespace Physics
{
bool ObjectLayerPairFilterImpl::ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const
{
    if (inLayer1 == Layers::NON_MOVING && inLayer2 == Layers::NON_MOVING)
        return false;

    // PLAYER collides with everything except other PLAYERs
    if (inLayer1 == Layers::PLAYER && inLayer2 == Layers::PLAYER)
        return false;

    // SENSOR only collides with PLAYER
    if (inLayer1 == Layers::SENSOR || inLayer2 == Layers::SENSOR) {
        return inLayer1 == Layers::PLAYER || inLayer2 == Layers::PLAYER;
    }

    return true;
}
} // Physics
