//
// Created by William on 2026-01-30.

#ifndef WILL_ENGINE_DEBUG_COMPONENTS_H
#define WILL_ENGINE_DEBUG_COMPONENTS_H

#include "engine/reflection/reflection.h"

namespace Game::Component
{
struct MotionBlurMovementComponent
{
    static constexpr const char* COMPONENT_NAME = "MotionBlurMovementComponent";

    bool bIsHorizontal{false};

    WILL_REFLECT(MotionBlurMovementComponent, WILL_FIELD(bIsHorizontal))
};
struct AntiGravityTag
{
    static constexpr const char* COMPONENT_NAME = "AntiGravityTag";
};
struct FloorTag
{
    static constexpr const char* COMPONENT_NAME = "FloorTag";
};
}

#endif //WILL_ENGINE_DEBUG_COMPONENTS_H
