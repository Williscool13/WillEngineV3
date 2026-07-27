//
// Created by William on 2026-07-09.
//

#ifndef WILL_ENGINE_INSTANCE_MASK_INTEROP_H
#define WILL_ENGINE_INSTANCE_MASK_INTEROP_H

#ifdef __SLANG__
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#else
#include <cstdint>

using uint = uint32_t;

#define SHADER_PUBLIC
#define SHADER_CONST constexpr inline
#endif // __SLANG__

// Scene occluder geometry.
SHADER_PUBLIC SHADER_CONST uint INSTANCE_MASK_OCCLUDER = 0x01u;
// Area/sphere light emissive representative meshes. Excluded from generic occlusion so lights don't self-shadow; the DDGI probe trace reads their emission directly.
SHADER_PUBLIC SHADER_CONST uint INSTANCE_MASK_LIGHT_PROXY = 0x02u;
// Occluder geometry with DDGI contribution enabled (PrimitiveInstanceData::ddgiVisible / a mesh's "DDGI Contribution" checkbox).
SHADER_PUBLIC SHADER_CONST uint INSTANCE_MASK_DDGI = 0x04u;
// Occluder geometry that casts the traced (stochastic + denoised) sun shadow.
SHADER_PUBLIC SHADER_CONST uint INSTANCE_MASK_SUN_OCCLUDER = 0x08u;

SHADER_PUBLIC SHADER_CONST uint INSTANCE_MASK_ALL = 0xFFu;


SHADER_PUBLIC SHADER_CONST uint INSTANCE_MASK_DDGI_PROBE_TRACE = INSTANCE_MASK_DDGI | INSTANCE_MASK_LIGHT_PROXY;

#endif //WILL_ENGINE_INSTANCE_MASK_INTEROP_H
