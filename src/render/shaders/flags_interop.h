//
// Created by William on 2026-07-21.
//

#ifndef WILL_ENGINE_FLAGS_INTEROP_H
#define WILL_ENGINE_FLAGS_INTEROP_H

#ifdef __SLANG__
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#else
#include <cstdint>

using uint = uint32_t;

#define SHADER_PUBLIC
#define SHADER_CONST constexpr inline
#endif // __SLANG__

// gbuffer_one.w bit layout: bits 0-2 = flags below, bits 3-15 free, bits 16-31 = packed viewZ delta (PackViewZDelta).
SHADER_PUBLIC SHADER_CONST uint GBUFFER1_LIT_BIT = 0x1u;
SHADER_PUBLIC SHADER_CONST uint GBUFFER1_EMISSIVE_BIT = 0x2u;
SHADER_PUBLIC SHADER_CONST uint GBUFFER1_NO_MOTION_BLUR_BIT = 0x4u;

// Instance::flags bits (model_interop.h)
SHADER_PUBLIC SHADER_CONST uint INSTANCE_FLAG_MOTION_BLUR = 0x1u;
SHADER_PUBLIC SHADER_CONST uint INSTANCE_FLAG_ALPHA_CUTOUT = 0x2u;
SHADER_PUBLIC SHADER_CONST uint INSTANCE_FLAG_DDGI_VISIBLE = 0x4u;

#endif // WILL_ENGINE_FLAGS_INTEROP_H
