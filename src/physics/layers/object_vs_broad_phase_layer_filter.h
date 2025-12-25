//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_OBJECT_VS_BROAD_PHASE_LAYER_FILTER_H
#define WILL_ENGINE_OBJECT_VS_BROAD_PHASE_LAYER_FILTER_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

namespace Physics
{
class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    ~ObjectVsBroadPhaseLayerFilterImpl() override = default;

    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override;
};
} // Physics

#endif //WILL_ENGINE_OBJECT_VS_BROAD_PHASE_LAYER_FILTER_H