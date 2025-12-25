//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_TRANSFORM_COMPONENT_H
#define WILL_ENGINE_TRANSFORM_COMPONENT_H

#include "core/math/transform.h"

namespace Game
{
struct TransformComponent
{
    Transform transform{Transform::IDENTITY};
};
}

#endif //WILL_ENGINE_TRANSFORM_COMPONENT_H
