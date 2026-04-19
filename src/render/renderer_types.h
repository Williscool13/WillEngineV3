//
// Created by William on 2026-04-19.
//

#ifndef WILL_ENGINE_RENDERER_TYPES_H
#define WILL_ENGINE_RENDERER_TYPES_H
#include "core/string_id.h"

struct VisibilityBufferTargets
{
    StringID visibility;
    StringID stableId;
    StringID velocity;
    StringID depthStencil;
};
struct VisibilityBufferBarycentricDerivativeTargets
{
    StringID visibility; // in
    StringID barycentric; // out
    StringID derivatives; // out
};
struct VisibilityShadingTargets
{
    StringID visibility; // in
    StringID barycentric; // in
    StringID derivatives; // in
    StringID albedo; // out
    StringID normal; // out
    StringID pbr; // out
    StringID emissive; // out
};

struct DeferredResolveTargets
{
    StringID albedo; // in
    StringID normal; // in
    StringID pbr; // in
    StringID emissive; // in
    StringID depthStencil; // in
    StringID shadows; // in (optional, leave default-constructed if unavailable)
    StringID output; // out
};
struct MainRenderTargets
{
    StringID normal;
    StringID depthStencil;
    // todo: velocity
    StringID velocity;
    StringID outputColor;
};

#endif //WILL_ENGINE_RENDERER_TYPES_H
