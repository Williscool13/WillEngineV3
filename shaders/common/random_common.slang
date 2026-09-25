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
    float t = float(2654435769u * frameIndex) * (1.0 / 4294967296.0);
    float n = InterleavedGradientNoise(float2(pixel) + (5.588238 * 64.0) * t);
    return n * 6.28318530718;
}

/** 2D sample in (0, 1)^2 from the baked spatiotemporal blue noise: one 128x128 slice per frame (64-frame loop), blue within each frame and along each pixel's sequence of frames. Salt toroidally shifts the lookup so concurrent streams stay decorrelated without losing blueness. */
public float2 SpatioTemporalBlueNoise2D(uint2 pixel, uint frameIndex, uint salt = 0u)
{
    uint slice = frameIndex & BLUE_NOISE_SLICE_MASK;
    uint2 lookup = ((pixel + uint2(salt * 53u, salt * 97u)) & BLUE_NOISE_SIZE_MASK) + uint2(slice & 7u, slice >> 3u) * BLUE_NOISE_SIZE;
    float2 ranks = assetTextures[BLUE_NOISE_BINDLESS_INDEX].Load(int3(int2(lookup), 0)).rg;
    return (ranks * 255.0 + 0.5) / 256.0;
}
