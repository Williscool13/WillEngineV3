//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_BROAD_PHASE_LAYER_INTERFACE_H
#define WILL_ENGINE_BROAD_PHASE_LAYER_INTERFACE_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

#include "layer_interface.h"

namespace Physics
{
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl();

    ~BPLayerInterfaceImpl() override = default;

    [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override;

    [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override;

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const
    {
        switch ((JPH::BroadPhaseLayer::Type) inLayer) {
            case (JPH::BroadPhaseLayer::Type) BroadPhaseLayers::STATIC: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type) BroadPhaseLayers::DYNAMIC: return "MOVING";
            default: JPH_ASSERT(false);
                return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS]{};
};
} // Physics

#endif //WILL_ENGINE_BROAD_PHASE_LAYER_INTERFACE_H