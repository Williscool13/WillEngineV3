//
// Created by William on 2026-10-05.
//

#ifndef WILL_ENGINE_RECORD_FRAME_CONTEXT_H
#define WILL_ENGINE_RECORD_FRAME_CONTEXT_H

#include "render/renderer_types.h"
#include "render/interface/render_interface.h"
#include "render/post-processing/post_processing.h"
#include "render/types/render_types.h"

namespace Render
{
struct FrameFeatures
{
    bool bGeometry{false};
    bool bGroundTruth{false};
    bool bReflectionScreenSpace{false};
    bool bGIGatherScreenSpace{false};
    bool bFsr2Reactive{false};
    bool bSnapshotLitColor{false};
    bool bVolumetricFog{false};
    bool bFoggedLitCopy{false};
    bool bNeedsWorldGrid{false};
    bool bDDGI{false};
    bool bDDGIApply{false};
    bool bGIGather{false};
    bool bSunViaReSTIR{false};
    bool bRTSun{false};
};

/** Per-frame state shared by RecordFrame's phases; it outlives Execute, so recorded lambdas may reference it. */
struct FrameContext
{
    Core::FrameBuffer& frameBuffer;
    Core::ViewFamily& viewFamily;
    uint32_t frameIndex{0};

    Core::Array<uint32_t, 2> renderExtent{};
    Core::Array<uint32_t, 2> outputExtent{};
    // Extent after AA. Differs from renderExtent only for upscaling AA
    Core::Array<uint32_t, 2> postAaExtent{};
    float displayAspect{1.0f};
    PaniniParams displayPanini{};

    RenderFamilyProperties properties{};
    RenderTargets targets{};
    FrameFeatures features{};

    // Set while recording lighting
    uint32_t restirCheckerboardField{0};
    uint32_t giGatherMode{0};
};
} // Render

#endif //WILL_ENGINE_RECORD_FRAME_CONTEXT_H
