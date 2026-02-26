//
// Created by William on 2026-01-30.

#ifndef WILL_ENGINE_DEBUG_COMPONENTS_H
#define WILL_ENGINE_DEBUG_COMPONENTS_H

/**
 * A collection of mostly throwaway components for debugging/temporary setups.
 */
namespace Game::Component
{
struct MotionBlurMovementComponent
{
    bool bIsHorizontal{false};
};
struct AntiGravityComponent
{};
struct FloorComponent
{};

struct CubemapVisualizeTag{};
}

#endif //WILL_ENGINE_DEBUG_COMPONENTS_H
