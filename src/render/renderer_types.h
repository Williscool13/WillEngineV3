//
// Created by William on 2026-04-19.
//

#ifndef WILL_ENGINE_RENDERER_TYPES_H
#define WILL_ENGINE_RENDERER_TYPES_H
#include "core/string_id.h"
#include "render/render-graph/render_graph_handles.h"

// Cull tallies, dispatch counts, radiance-cache stats
#if WILL_EDITOR
inline constexpr bool GPU_STATS_ENABLED = true;
#else
inline constexpr bool GPU_STATS_ENABLED = false;
#endif

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

/** Per-frame targets shared across systems. Histories are last frame's versions and are invalid when nothing was produced then. */
struct RenderTargets
{
    Render::RDGTexture visibility;

    // GBuffer
    Render::RDGTexture gbufferOne;
    Render::RDGTexture gbufferOneHistory;
    Render::RDGTexture gbufferTwo;
    Render::RDGTexture shadowOriginOffset;
    Render::RDGTexture shadows;

    // Any purpose textures for use between gbuffer and color output. Same format as color output.
    Render::RDGTexture intermediateOne;
    Render::RDGTexture intermediateTwo;

    Render::RDGTexture colorOutput;
    Render::RDGTexture depthStencil;
    Render::RDGTexture depthCopy;
    Render::RDGTexture depthCopyHistory;
    Render::RDGTexture stableId;

    // Lit scene before fog and overlays, written by the Lit Color Snapshot
    Render::RDGTexture litSnapshot;
    Render::RDGTexture litSnapshotHistory;
    // litSnapshot without fog, LIT_COLOR_PREOVERLAY with fog. Invalid when neither FSR2 nor exposure metering reads it
    Render::RDGTexture preOverlayColor;

    // Declared in RecordFrameSetup when the ReSTIR path runs the screen-space gather
    Render::RDGTexture restirDiffuseRatio;
    Render::RDGTexture giScreenDiffuse;
    Render::RDGTexture giScreenDiffuseHistory;

    // SetupObjectMotion output, valid when the gather or motion blur reads it
    Render::RDGTexture objectMotion;
    // Written by the reflection denoise, read by FSR2 and motion blur
    Render::RDGTexture reflectionVirtualMotion;
};

#endif //WILL_ENGINE_RENDERER_TYPES_H
