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
    StringID gbufferTwo; // color attachment: R=0, G=packed motion vectors (R16G16 half-float)
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
    StringID gbufferOne; // out: R=albedo RGB8, G=normal oct16, B=emissive RGBE
    StringID gbufferTwo; // read-write: R=roughness/metal (write), G=motion vectors (preserve)
};

struct DeferredResolveTargets
{
    StringID gbufferOne; // in: R=albedo, G=normal, B=emissive
    StringID gbufferTwo; // in: R=roughness/metal, G=motion vectors
    StringID depthStencil; // in
    StringID shadows; // in (optional, leave default-constructed if unavailable)
    StringID output; // out
};
struct MainRenderTargets
{
    StringID gbufferOne; // for GTAO + shadow resolve (G=normal oct16)
    StringID gbufferTwo; // for motion blur (G=packed motion vectors)
    StringID depthStencil;
    StringID outputColor;
};

#endif //WILL_ENGINE_RENDERER_TYPES_H
