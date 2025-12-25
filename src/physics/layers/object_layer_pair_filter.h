//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_OBJECT_LAYER_PAIR_FILTER_H
#define WILL_ENGINE_OBJECT_LAYER_PAIR_FILTER_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

namespace Physics
{
class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
{
public:
    ~ObjectLayerPairFilterImpl() override = default;

    /**
     * Should do physics checks against specified layer?
     * @param inLayer1
     * @param inLayer2
     * @return
     */
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override;
};
} // Physics

#endif //WILL_ENGINE_OBJECT_LAYER_PAIR_FILTER_H
