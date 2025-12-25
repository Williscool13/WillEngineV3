//
// Created by William on 2025-12-25.
//

#include "object_layer_pair_filter.h"

#include "layer_interface.h"

namespace Physics
{
bool ObjectLayerPairFilterImpl::ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const
{
    return !(inLayer1 == Layers::STATIC && inLayer2 == Layers::STATIC);
}
} // Physics
