module restir_common;

import shader_constants;
import lights_interop;

/** Area light data transformed into view space, with derived geometry cached. */
public struct AreaLightVS
{
    public float3 center;
    public float3 normal;
    public float3 right;
    public float3 up;
    public float halfWidth;
    public float halfHeight;
    public float area;
}

/**
 * Transforms an AreaLightData into view space.
 *
 * @param al World-space area light.
 * @param view View matrix (float4x4).
 */
public AreaLightVS TransformAreaLight(AreaLightData al, float4x4 view)
{
    AreaLightVS vs;
    vs.center = mul(view, float4(al.position.xyz, 1.0)).xyz;
    vs.normal = normalize(mul((float3x3)view, al.normal.xyz));
    vs.right = normalize(mul((float3x3)view, al.right.xyz));
    vs.up = normalize(mul((float3x3)view, al.up.xyz));
    vs.halfWidth = al.right.w;
    vs.halfHeight = al.up.w;
    vs.area = 4.0 * vs.halfWidth * vs.halfHeight;
    return vs;
}

/** Geometric quantities for a point on an area light as seen from a shading point. */
public struct AreaLightSample
{
    public float3 samplePos;
    public float3 L;
    public float dist;
    public float cosShade;
    public float cosLight;
    public float attenuation;
}

/**
 * Evaluates geometry for a specific sample position on an area light.
 *
 * @param vs View-space area light.
 * @param samplePos Point on the light surface (view space).
 * @param shadingPos Shading point (view space).
 * @param N Shading normal.
 * @param range Light range for attenuation falloff.
 */
public AreaLightSample EvalAreaLightSample(AreaLightVS vs, float3 samplePos, float3 shadingPos, float3 N, float range)
{
    AreaLightSample s;
    s.samplePos  = samplePos;
    float3 toSample = samplePos - shadingPos;
    s.dist = length(toSample);
    s.L = toSample / s.dist;
    s.cosShade = max(dot(N, s.L), 0.0);
    s.cosLight = max(dot(-vs.normal, s.L), 0.0);
    float a = 1.0 - smoothstep(0.0, range, s.dist);
    s.attenuation = a * a;
    return s;
}

/**
 * Returns a random point on the light surface.
 *
 * @param vs View-space area light.
 * @param uU Uniform random in [0, 1) along right axis.
 * @param uV Uniform random in [0, 1) along up axis.
 */
public float3 SampleAreaLightPos(AreaLightVS vs, float uU, float uV)
{
    return vs.center + (uU * 2.0 - 1.0) * vs.halfWidth * vs.right + (uV * 2.0 - 1.0) * vs.halfHeight * vs.up;
}

/** PCG-style hash step. Advances the state and returns a uniform float in [0, 1). */
public float HashStep(inout uint h)
{
    h = h ^ (h >> 17u); h *= 0xbf324c81u; h = h ^ (h >> 13u); h *= 0x9c7d2d49u; h = h ^ (h >> 16u);
    return float(h & 0xFFFFu) / 65536.0;
}

public struct ReservoirSample
{
    public int lightIdx;
    public float3 samplePos;
    public float attenuation;
}

/**
 * A single ReSTIR reservoir storing one selected light sample.
 *
 * @param y     Selected sample index, or -1 if empty.
 * @param wSum  Running sum of candidate weights during WRS streaming update.
 * @param M     Number of candidates seen so far.
 * @param W     Unbiased contribution weight: wSum / (M * targetPdf(y)). Multiply shading result by W to get the unbiased estimate.
 */
public struct Reservoir
{
    public int32_t y;
    public float wSum;
    public int32_t M;
    public float W;
}

public Reservoir ReservoirEmpty()
{
    Reservoir r;
    r.y = -1;
    r.wSum = 0.0;
    r.M = 0;
    r.W = 0.0;
    return r;
}

/**
 * Streaming WRS update for a single new candidate.
 *
 * @param r Reservoir to update.
 * @param candidateY Index of the incoming candidate.
 * @param candidateWeight w_i = targetPdf(candidateY) / sourcePdf(candidateY).
 * @param u Uniform random float in [0, 1).
 * @return True if the candidate was selected.
 */
public bool ReservoirAddSample(inout Reservoir r, int candidateY, float candidateWeight, float u)
{
    r.wSum += candidateWeight;
    r.M += 1;
    if (u < candidateWeight / r.wSum)
    {
        r.y = candidateY;
        return true;
    }
    return false;
}

/**
 * Merges another reservoir into this one (temporal/spatial reuse). The incoming reservoir contributes its full M to the confidence count.
 *
 * @param r Reservoir to merge into.
 * @param other Reservoir to merge in.
 * @param targetPdfY p_hat evaluated at other.y from this pixel's perspective.
 * @param u Uniform random float in [0, 1).
 * @return True if other.y was selected.
 */
public bool ReservoirMerge(inout Reservoir r, Reservoir other, float targetPdfY, float u)
{
    float candidateWeight = targetPdfY * other.W * float(other.M);
    r.wSum += candidateWeight;
    r.M += other.M;
    if (u < candidateWeight / r.wSum)
    {
        r.y = other.y;
        return true;
    }
    return false;
}

/**
 * Finalises W after all candidates/merges have been streamed.
 *
 * @param r Reservoir to finalise.
 * @param targetPdfY p_hat evaluated at the selected sample y.
 */
public void ReservoirFinalise(inout Reservoir r, float targetPdfY)
{
    r.W = (targetPdfY > 0.0) ? (r.wSum / targetPdfY) : 0.0;
}

/**
 * Evaluates the GGX solid-angle PDF for a given light direction L.
 * Matches the distribution sampled by ImportanceSampleGGX.
 *
 * @param N         Shading normal.
 * @param V         View direction (toward camera).
 * @param L         Light direction to evaluate.
 * @param roughness Perceptual roughness (squared internally).
 * @return          pdf >= 0. Returns 0 if L is below the hemisphere or H is degenerate.
 */
public float GGXPdf(float3 N, float3 V, float3 L, float roughness)
{
    if (dot(N, L) <= 0.0) { return 0.0; }
    float3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0001);
    float a = roughness * roughness;
    float a2 = a * a;
    float D = a2 / (PI * max(pow(NdotH * NdotH * (a2 - 1.0) + 1.0, 2.0), 0.0001));
    return D * NdotH / (4.0 * VdotH);
}

/**
 * GGX importance sampling. Samples a direction L from the specular lobe around N.
 *
 * @param xi Two uniform random floats in [0, 1).
 * @param N Shading normal (view space).
 * @param V View direction (view space, pointing toward camera).
 * @param roughness Perceptual roughness (squared internally).
 * @param pdf Output: solid-angle pdf of the returned L direction.
 * @return Sampled light direction L (view space). May be below the hemisphere; caller must check dot(N, L) > 0.
 */
public float3 ImportanceSampleGGX(float2 xi, float3 N, float3 V, float roughness, out float pdf)
{
    float a = roughness * roughness;
    float a2 = a * a;

    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / max(1.0 + (a2 - 1.0) * xi.y, 0.0001));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));

    float3 Ht = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    float3 H = normalize(T * Ht.x + B * Ht.y + N * Ht.z);

    float3 L = reflect(-V, H);

    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0001);
    float D = a2 / (PI * max(pow(NdotH * NdotH * (a2 - 1.0) + 1.0, 2.0), 0.0001));
    pdf = D * NdotH / (4.0 * VdotH);

    return L;
}

/**
 * Ray vs. axis-aligned quad intersection test.
 *
 * @param rayOrigin  Ray origin.
 * @param rayDir     Ray direction (need not be normalised).
 * @param center     Quad center.
 * @param normal     Quad normal (must face the side the ray comes from for a valid hit).
 * @param right      Quad right axis (unit length).
 * @param up         Quad up axis (unit length).
 * @param halfWidth  Half-extent along right.
 * @param halfHeight Half-extent along up.
 * @param t          Output: ray parameter at intersection. Negative if miss or behind origin.
 * @return           True if the ray hits the quad at t > 0.
 */
public bool RayIntersectQuad(float3 rayOrigin, float3 rayDir, float3 center, float3 normal, float3 right, float3 up, float halfWidth, float halfHeight, out float t)
{
    float denom = dot(normal, rayDir);
    if (abs(denom) < 0.0001) { t = -1.0; return false; }
    t = dot(normal, center - rayOrigin) / denom;
    if (t <= 0.0) { return false; }
    float3 hitPos = rayOrigin + t * rayDir;
    float3 local = hitPos - center;
    return abs(dot(local, right)) <= halfWidth && abs(dot(local, up)) <= halfHeight;
}
