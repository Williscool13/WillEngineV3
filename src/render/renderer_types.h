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
    StringID gbufferOne; // color attachment: R=0, G=packed motion vectors, B=0, A=0
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
    StringID gbufferOne; // read-write: R=normal oct16, G=motion vectors (preserve), B=pbr
    StringID gbufferTwo; // out: R=albedo RGB8, G=emissive RGBE
};

struct DeferredResolveTargets
{
    StringID gbufferOne; // in: R=normal oct16, G=velocity, B=pbr
    StringID gbufferTwo; // in: R=albedo, G=emissive
    StringID depthStencil; // in
    StringID shadows; // in (optional, leave default-constructed if unavailable)
    StringID output; // out
};
struct MainRenderTargets
{
    StringID gbufferOne; // for GTAO + shadow resolve (R=normal oct16) and deferred resolve
    StringID gbufferTwo; // R=albedo, G=emissive
    StringID depthStencil;
    StringID outputColor;
};

#endif //WILL_ENGINE_RENDERER_TYPES_H
