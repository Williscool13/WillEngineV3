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

SHADER_PUBLIC struct SHADER_ALIGN GlyphQuad
{
    SHADER_PUBLIC float2 posMin;
    SHADER_PUBLIC float2 posMax;
    SHADER_PUBLIC float2 uvMin;
    SHADER_PUBLIC float2 uvMax;
    SHADER_PUBLIC float4 color;
    SHADER_PUBLIC float2 uvOrigMin;
    SHADER_PUBLIC float2 uvOrigMax;
    SHADER_PUBLIC uint32_t drawCallIndex;
    SHADER_PUBLIC uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};

// GPU-side text material. Uploaded as a flat buffer; indexed by TextInstanceData.textMaterialIndex.
SHADER_PUBLIC struct SHADER_ALIGN TextRenderMaterial
{
    SHADER_PUBLIC float4 colorTint;
    SHADER_PUBLIC float4 outlineColor;
    SHADER_PUBLIC float outlineWidth;
    SHADER_PUBLIC float shadowSoftness;
    SHADER_PUBLIC float2 shadowOffset;
    SHADER_PUBLIC float4 shadowColor;
};

SHADER_PUBLIC struct SHADER_ALIGN TextInstanceData
{
    SHADER_PUBLIC uint32_t modelIndex;
    SHADER_PUBLIC float pxRange;
    SHADER_PUBLIC uint32_t atlasBindlessIndex;
    SHADER_PUBLIC uint32_t textMaterialIndex;
    SHADER_PUBLIC uint32_t _pad0;
    SHADER_PUBLIC uint32_t _pad1;
    SHADER_PUBLIC uint32_t _pad2;
    SHADER_PUBLIC uint32_t _pad3;
};

#endif //WILL_ENGINE_TEXT_INTEROP_H
