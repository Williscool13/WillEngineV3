module random_common;

import bindless_declarations;
import constants_interop;

public uint SeedRng(uint2 pixel, uint frameIndex, uint salt)
{
    return (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (frameIndex * 2654435761u) ^ salt;
}

/** Returns [0, 1). */
public float HashStep(inout uint h)
{
    h = h ^ (h >> 17u); h *= 0xbf324c81u; h = h ^ (h >> 13u); h *= 0x9c7d2d49u; h = h ^ (h >> 16u);
    return float(h >> 8) * (1.0f / float(1u << 24));
}

public float InterleavedGradientNoise(float2 p)
{
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

public float BlueNoiseAngle(uint2 pixel, uint frameIndex)
{
    float t = float(2654435769u * frameIndex) * (1.0 / 4294967296.0);
    float n = InterleavedGradientNoise(float2(pixel) + (5.588238 * 64.0) * t);
    return n * 6.28318530718;
}

/** Salt toroidally shifts the lookup so concurrent streams stay decorrelated. */
public float2 SpatioTemporalBlueNoise2D(uint2 pixel, uint frameIndex, uint salt = 0u)
{
    uint slice = frameIndex & BLUE_NOISE_SLICE_MASK;
    uint2 lookup = ((pixel + uint2(salt * 53u, salt * 97u)) & BLUE_NOISE_SIZE_MASK) + uint2(slice & 7u, slice >> 3u) * BLUE_NOISE_SIZE;
    float2 ranks = assetTextures[BLUE_NOISE_BINDLESS_INDEX].Load(int3(int2(lookup), 0)).rg;
    return (ranks * 255.0 + 0.5) / 256.0;
}
