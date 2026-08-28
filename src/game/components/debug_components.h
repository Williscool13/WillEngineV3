//
// Created by William on 2026-01-30.

#ifndef WILL_ENGINE_DEBUG_COMPONENTS_H
#define WILL_ENGINE_DEBUG_COMPONENTS_H

namespace Engine
{
class TextWriter;
class TextReader;
}

namespace Game::Component
{
struct MotionBlurMovementComponent
{
    static constexpr const char* COMPONENT_NAME = "MotionBlurMovementComponent";

    bool bIsHorizontal{false};

    static void Serialize(const MotionBlurMovementComponent& comp, Engine::TextWriter& w);
    static void Deserialize(MotionBlurMovementComponent& comp, const Engine::TextReader& r);
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
