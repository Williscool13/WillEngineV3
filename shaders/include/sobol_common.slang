module sobol_common;

import sobol_directions;

/**
 * Evaluates dimension @dim of the Sobol sequence at sample index @index.
 * Returns a raw uint before scrambling.
 */
uint SobolUint(uint index, uint dim)
{
    uint result = 0u;
    uint bit = 0u;
    for (uint i = index; i != 0u; i >>= 1u, bit++)
    {
        if ((i & 1u) != 0u) { result ^= SOBOL_DIRECTIONS[dim][bit]; }
    }
    return result;
}

/**
 * Burley 2020 hash-based Owen scramble (Laine-Karras permutation sandwiched
 * between reversebits calls). Preserves the stratification of the Sobol
 * sequence while decorrelating pixels and dimensions.
 *
 * @param v     Raw Sobol uint to scramble.
 * @param seed  Per-pixel, per-dimension seed.
 */
uint OwenScramble(uint v, uint seed)
{
    v = reversebits(v);
    v += seed;
    v ^= v * 0x6c50b47cu;
    v ^= v * 0xb82f1e52u;
    v ^= v * 0xc7afe638u;
    v ^= v * 0x8d22f6e6u;
    v = reversebits(v);
    return v;
}

/**
 * Returns a stratified uniform float in [0, 1) for the given Sobol sample
 * index and dimension, scrambled with @seed for per-pixel decorrelation.
 *
 * Assign each random variable a fixed dimension and advance the index per
 * candidate. Use a different seed component per dimension so adjacent dims
 * are not correlated even under the same pixel seed.
 *
 * @param index  Sample index within the sequence (0 = first candidate, etc.).
 * @param dim    Sobol dimension (0 = Van der Corput, 1-3 = Joe-Kuo).
 * @param seed   Base seed (combine pixel hash + frame hash before calling).
 */
public float SobolSample(uint index, uint dim, uint seed)
{
    uint v = SobolUint(index, dim);
    v = OwenScramble(v, seed ^ (dim * 0x9e3779b9u));
    return min(float(v) * (1.0f / 4294967296.0f), 0.99999994f);
}
