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
    Critical             = 1 << 0,
    Legacy               = 1 << 1,
    AssetGeneration      = 1 << 2,
    All = ~0U,
};

inline PipelineCategory operator|(PipelineCategory a, PipelineCategory b)
{
    return static_cast<PipelineCategory>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline PipelineCategory operator&(PipelineCategory a, PipelineCategory b)
{
    return static_cast<PipelineCategory>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasCategory(PipelineCategory flags, PipelineCategory check)
{
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(check)) == static_cast<uint32_t>(check);
}
} // Render

#endif //WILL_ENGINE_PIPELINE_CATEGORY_H
