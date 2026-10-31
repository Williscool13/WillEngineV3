//
// Created by William on 2026-10-31.
//

#ifndef WILL_ENGINE_FRAME_RESOURCES_H
#define WILL_ENGINE_FRAME_RESOURCES_H

namespace Engine
{
/**
 * Access names for ViewFamily fields with no type of their own. Systems reach them through the frame buffer, which AccessCheck cannot see, so these declarations are not verified.
 * Includes post process settings; volume overrides reach any of it by field name.
 */
struct ViewFamilySettings
{
    static constexpr const char* COMPONENT_NAME = "ViewFamilySettings";
};

struct ViewFamilyBakedShadowLights
{
    static constexpr const char* COMPONENT_NAME = "ViewFamilyBakedShadowLights";
};

struct ViewFamilyLights
{
    static constexpr const char* COMPONENT_NAME = "ViewFamilyLights";
};

struct ViewFamilyDebugDraws
{
    static constexpr const char* COMPONENT_NAME = "ViewFamilyDebugDraws";
};
} // Engine

#endif //WILL_ENGINE_FRAME_RESOURCES_H
