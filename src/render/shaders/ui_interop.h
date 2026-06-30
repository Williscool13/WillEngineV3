//
// Created by William on 2026-05-19.
//

#ifndef WILL_ENGINE_UI_INTEROP_H
#define WILL_ENGINE_UI_INTEROP_H

#ifdef __SLANG__
import common_interop;
#define SHADER_PUBLIC public
#else
#include "common_interop.h"
#endif // __SLANG__

SHADER_PUBLIC struct UIRectData
{
    SHADER_PUBLIC float4 color;
    SHADER_PUBLIC float2 posMin;
    SHADER_PUBLIC float2 posMax;
};

#endif //WILL_ENGINE_UI_INTEROP_H
