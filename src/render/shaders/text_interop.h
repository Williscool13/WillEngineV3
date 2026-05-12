//
// Created by William on 2026-05-12.
//

#ifndef WILL_ENGINE_TEXT_INTEROP_H
#define WILL_ENGINE_TEXT_INTEROP_H

#ifdef __SLANG__
module text_interop;
import common_interop;
#define SHADER_PUBLIC public
#define SHADER_ALIGN
#else
#include "common_interop.h"
#endif // __SLANG__

struct SHADER_ALIGN GlyphQuad
{
    SHADER_PUBLIC float2   posMin;
    SHADER_PUBLIC float2   posMax;
    SHADER_PUBLIC float2   uvMin;
    SHADER_PUBLIC float2   uvMax;
    SHADER_PUBLIC float4   color;
};

#ifndef __SLANG__
struct TextDrawCall
{
    uint32_t quadOffset;
    uint32_t quadCount;
    uint32_t modelIndex;
    uint32_t atlasBindlessIndex;
    uint32_t samplerIndex;
    float    screenPxRange;
};
#endif // __SLANG__

#endif //WILL_ENGINE_TEXT_INTEROP_H
