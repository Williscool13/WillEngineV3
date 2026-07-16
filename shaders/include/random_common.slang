module random_common;

import bindless_declarations;
import constants_interop;

/** Canonical per-pixel/per-frame RNG seed: hashes pixel coord, frame index, and a per-stream salt. */
public uint SeedRng(uint2 pixel, uint frameIndex, uint salt)
{
    return (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (frameIndex * 2654435761u) ^ salt;
}

/** PCG-style hash step. Advances the state and returns a uniform float in [0, 1). */
public float HashStep(inout uint h)
{
    h = h ^ (h >> 17u); h *= 0xbf324c81u; h = h ^ (h >> 13u); h *= 0x9c7d2d49u; h = h ^ (h >> 16u);
    return float(h >> 8) * (1.0f / float(1u << 24));
}

/** Jimenez interleaved gradient noise: a cheap, texture-free blue-noise-like value in [0, 1). */
public float InterleavedGradientNoise(float2 p)
{
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

/** Animated IGN rotation angle in [0, 2pi). Blue-noise distribution converges cleaner than a white-noise hash at equal taps. */
public float BlueNoiseAngle(uint2 pixel, uint frameIndex)
{
    float n = InterleavedGradientNoise(float2(pixel) + 5.588238 * float(frameIndex & 63u));
    return n * 6.28318530718;
}

/** Temporally-progressive 2D sample in [0, 1)^2: an R2 (plastic-constant) rank-1 lattice advances the pair across frames (low-discrepancy per pixel), Cranley-Patterson rotated by spatially-BLUE per-pixel offsets from the baked mask. White hash offsets let neighbouring texels clump directions within a frame (low-frequency flicker the spatial filters pass); IGN's diagonal isolines cross-hatch. */
public float2 SpatioTemporalBlueNoise2D(uint2 pixel, uint frameIndex)
{
    const float2 R2 = float2(0.7548776662466927, 0.5698402909980532);
    float2 seq = frac(R2 * float(frameIndex & 63u));
    float2 offsets = assetTextures[BLUE_NOISE_BINDLESS_INDEX].Load(int3(int2(pixel & BLUE_NOISE_SIZE_MASK), 0)).rg;
    return frac(seq + offsets);
}
