//
// Created by William on 2026-04-19.
//

#ifndef WILL_ENGINE_RENDERER_TYPES_H
#define WILL_ENGINE_RENDERER_TYPES_H
#include "core/string_id.h"

struct RendererStatistics
{
    uint64_t clippingInvocations{};
    uint64_t clippingPrimitives{};
    uint64_t fragmentInvocations{};
    uint64_t computeInvocations{};
    /**
     * Optional, depends on driver support. Will be ~0x0 if invalid.
     */
    uint64_t meshInvocations{};
};

struct RenderTargets
{
    StringID visibility;
    StringID barycentric;
    StringID derivatives;

    // GBuffer
    StringID gbufferOne;
    StringID gbufferTwo;
    StringID shadows;

    // Any purpose textures for use between gbuffer and color output. Same format as color output.
    StringID intermediateOne;
    StringID intermediateTwo;

    StringID colorOutput;
    StringID depthStencil;
    StringID stableId;
};

#endif //WILL_ENGINE_RENDERER_TYPES_H
