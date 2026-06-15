module random_common;

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
