//
// Created by William on 2026-10-05.
//

#ifndef WILL_ENGINE_RECORD_FRAME_CONTEXT_H
#define WILL_ENGINE_RECORD_FRAME_CONTEXT_H

#include "render/renderer_types.h"
#include "render/frame_outputs.h"
#include "render/passes/debug_passes.h"
#include "render/interface/render_interface.h"
#include "render/post-processing/post_processing.h"
#include "render/types/render_types.h"
#include "core/types/extent.h"

namespace Render
{
enum class FrameRenderingPath : uint8_t
{
    Analytic,
    ReSTIR,
    PathTracing,
    GroundTruth,
};

enum class SunShadowSource : uint8_t
{
    None,
    RayTraced,
    ReSTIR,
};

enum class DDGIUsage : uint8_t
{
    Off,
    ProbesOnly,
    Applied,
};

struct FrameFeatures
{
    SunShadowSource sunShadow{SunShadowSource::None};
    DDGIUsage ddgi{DDGIUsage::Off};
    bool bGIGather{false};
    bool bGTAO{false};
    bool bVolumetricFog{false};

    bool DDGIApplied() const { return ddgi == DDGIUsage::Applied; }
};

struct FrameNeeds
{
    bool bWorldGrid{false};
    // LIT_COLOR_HISTORY: lit scene before fog, read next frame by the screen-space reflection and gather tiers
    bool bLitHistory{false};
    // LIT_COLOR_PREOVERLAY: lit frame after fog, before overlays, read by FSR2 reactive mask and exposure metering
    bool bPreOverlayColor{false};
};

/** Per-frame state shared by RecordFrame's phases; it outlives Execute, so recorded lambdas may reference it. */
struct FrameContext
{
    Core::FrameBuffer& frameBuffer;
    Core::ViewFamily& viewFamily;
    uint32_t frameIndex{0};

    Core::Extent2D renderExtent{};
    Core::Extent2D outputExtent{};
    // Extent after AA. Differs from renderExtent only for upscaling AA
    Core::Extent2D postAaExtent{};
    float displayAspect{1.0f};
    PaniniParams displayPanini{};

    bool bCanRender{false};
    bool bHasScene{false};
    FrameRenderingPath path{FrameRenderingPath::Analytic};
    FrameFeatures features{};
    FrameNeeds needs{};

    SceneBufferSizes bufferSizes{};
    SceneResources scene{};
    RenderTargets targets{};

    // System outputs, each invalid until (and unless) its system records this frame
    GeometryFrame geometry{};
    WorldGridFrame worldGrid{};
    GPUDebugFrame gpuDebug{};
    GPUDebugLines gpuDebugLines{};
    RadianceCacheFrame radianceCache{};
    DDGIFrame ddgi{};
    GTAOFrame gtao{};
    FinalGatherFrame gather{};
    ReflectionFrame reflection{};
    ReSTIRFrame restir{};
    SunShadowFrame sunShadow{};

    // Set while recording lighting
    uint32_t restirCheckerboardField{0};
    uint32_t giGatherMode{0};
};
} // Render

#endif //WILL_ENGINE_RECORD_FRAME_CONTEXT_H
