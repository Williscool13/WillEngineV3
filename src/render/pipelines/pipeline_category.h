//
// Created by William on 2026-01-20.
//

#ifndef WILL_ENGINE_PIPELINE_CATEGORY_H
#define WILL_ENGINE_PIPELINE_CATEGORY_H
#include <cstdint>

namespace Render
{
enum class PipelineCategory : uint32_t
{
    None                 = 0,
    Instancing           = 1 << 0,
    Geometry             = 1 << 1,
    Shadow               = 1 << 2,
    GTAO                 = 1 << 3,
    ShadowCombine        = 1 << 4,
    DeferredShading      = 1 << 5,
    TAA                  = 1 << 6,
    CustomStencilPass    = 1 << 7,
    PortalRendering      = 1 << 8,
    Exposure             = 1 << 9,
    Bloom                = 1 << 10,
    MotionBlur           = 1 << 11,
    Tonemap              = 1 << 12,
    ColorGrade           = 1 << 13,
    Vignette             = 1 << 14,
    FilmGrain            = 1 << 15,
    Sharpening           = 1 << 16,
    Debug                = 1 << 17,

    MainGeometry= Instancing | Geometry,
    ShadowPass  = Instancing | Shadow,
    PostProcess = TAA | Exposure | Bloom | MotionBlur | Tonemap | ColorGrade | Vignette | FilmGrain | Sharpening | GTAO,
    All         = ~0U,
};

inline PipelineCategory operator|(PipelineCategory a, PipelineCategory b) {
    return static_cast<PipelineCategory>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline PipelineCategory operator&(PipelineCategory a, PipelineCategory b) {
    return static_cast<PipelineCategory>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasCategory(PipelineCategory flags, PipelineCategory check) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(check)) == static_cast<uint32_t>(check);
}
} // Render

#endif //WILL_ENGINE_PIPELINE_CATEGORY_H