//
// Created by William on 2025-12-25.
//

#include "broad_phase_layer_interface.h"

namespace Physics
{
BPLayerInterfaceImpl::BPLayerInterfaceImpl()
{
    mObjectToBroadPhase[Layers::STATIC] = BroadPhaseLayers::STATIC;
    mObjectToBroadPhase[Layers::DYNAMIC] = BroadPhaseLayers::DYNAMIC;
}

JPH::uint BPLayerInterfaceImpl::GetNumBroadPhaseLayers() const
{
    return BroadPhaseLayers::NUM_LAYERS;
}

JPH::BroadPhaseLayer BPLayerInterfaceImpl::GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const
{
    JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
    return mObjectToBroadPhase[inLayer];
}

} // Physics