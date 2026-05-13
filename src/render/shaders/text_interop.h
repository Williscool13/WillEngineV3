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
    SHADER_PUBLIC float2 posMin;
    SHADER_PUBLIC float2 posMax;
    SHADER_PUBLIC float2 uvMin;
    SHADER_PUBLIC float2 uvMax;
    SHADER_PUBLIC float4 color;
    SHADER_PUBLIC uint32_t drawCallIndex;
    SHADER_PUBLIC uint32_t _pad0;
    SHADER_PUBLIC uint32_t _pad1;
    SHADER_PUBLIC uint32_t _pad2;
};

struct SHADER_ALIGN TextInstanceData
{
    SHADER_PUBLIC uint32_t modelIndex;
    SHADER_PUBLIC float pxRange;
    SHADER_PUBLIC uint32_t atlasBindlessIndex;
    uint32_t pad;
};

#endif //WILL_ENGINE_TEXT_INTEROP_H
