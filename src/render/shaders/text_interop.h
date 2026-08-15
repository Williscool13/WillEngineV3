//
// Created by William on 2026-05-12.
//

#ifndef WILL_ENGINE_TEXT_INTEROP_H
#define WILL_ENGINE_TEXT_INTEROP_H

#ifdef __SLANG__
import common_interop;
#define SHADER_PUBLIC public
#else
#include "common_interop.h"
#endif // __SLANG__

SHADER_PUBLIC struct WorldGlyphQuad
{
    SHADER_PUBLIC float4 color;
    SHADER_PUBLIC float2 posMin;
    SHADER_PUBLIC float2 posMax;
    SHADER_PUBLIC float2 emMin;
    SHADER_PUBLIC float2 emMax;
    SHADER_PUBLIC uint32_t glyphTexelOffset;
    SHADER_PUBLIC uint32_t drawCallIndex;
    uint32_t _pad0;
    uint32_t _pad1;
};

// GPU-side text material. Uploaded as a flat buffer; indexed by pc.textMaterialIndex.
SHADER_PUBLIC struct TextRenderMaterial
{
    SHADER_PUBLIC float4 colorTint;
    SHADER_PUBLIC float4 outlineColor;
    SHADER_PUBLIC float4 shadowColor;
    SHADER_PUBLIC float2 shadowOffset;
    SHADER_PUBLIC float outlineWidth;
    SHADER_PUBLIC float shadowSoftness;
};

SHADER_PUBLIC struct TextInstanceData
{
    SHADER_PUBLIC uint32_t modelIndex;
    SHADER_PUBLIC uint32_t stableIdLo;
    SHADER_PUBLIC uint32_t stableIdHi;
    uint32_t _pad0;
};

SHADER_PUBLIC struct UIGlyphQuad
{
    SHADER_PUBLIC float4 color;
    SHADER_PUBLIC float2 posMin;
    SHADER_PUBLIC float2 posMax;
    SHADER_PUBLIC float2 emMin;
    SHADER_PUBLIC float2 emMax;
    SHADER_PUBLIC uint32_t glyphTexelOffset;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};

#endif //WILL_ENGINE_TEXT_INTEROP_H
