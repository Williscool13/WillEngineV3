module restir_common;

import shader_constants;
import lights_interop;
import restir_interop;
import pbr_functions;

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
    s.samplePos = samplePos;
    float3 toSample = samplePos - shadingPos;
    s.dist = max(length(toSample), 1e-5);
    s.L = toSample / s.dist;
    s.cosShade = max(dot(N, s.L), 0.0);
    s.cosLight = max(dot(-vs.normal, s.L), 0.0);

    // inv squared falloff
    //float distSq = s.dist * s.dist;
    //float rangeInvSq = 1.0 / max(range * range, 1e-8);
    //float falloff = saturate(1.0 - distSq * rangeInvSq);
    //s.attenuation = (1.0 / (distSq + 1.0)) * falloff * falloff;

    // artist authored distance falloff
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
    return float(h >> 8) * (1.0f / float(1u << 24));
}

public Reservoir ReservoirEmpty()
{
    Reservoir r;
    r.sampleOffsetPacked = 0u;
    r.lightIdx = ~0u;
    r.W = 0.0;
    r.M = 0u;
    return r;
}

/**
 * Encodes a view-space sample position as snorm16x2 packed into a uint.
 * Normalizes by half-extents so the stored value is in [-1, 1] along each axis.
 */
public uint EncodeSampleOffset(float3 samplePos, AreaLightVS vs)
{
    float3 delta = samplePos - vs.center;
    float u = clamp(dot(delta, vs.right) / vs.halfWidth, -1.0, 1.0);
    float v = clamp(dot(delta, vs.up) / vs.halfHeight, -1.0, 1.0);
    uint ux = uint(int(u * 32767.0) & 0xFFFF);
    uint vy = uint(int(v * 32767.0) & 0xFFFF);
    return ux | (vy << 16);
}

/**
 * Reconstructs the view-space sample position from a reservoir's packed snorm16x2 offset.
 */
public float3 DecodeSamplePos(Reservoir r, AreaLightVS vs)
{
    int ix = int(r.sampleOffsetPacked << 16) >> 16;
    int iy = int(r.sampleOffsetPacked) >> 16;
    float u = max(float(ix) / 32767.0, -1.0);
    float v = max(float(iy) / 32767.0, -1.0);
    return vs.center + u * vs.halfWidth * vs.right + v * vs.halfHeight * vs.up;
}

/**
 * Transient accumulator used during candidate generation. Not written to the output buffer.
 *
 * @param wSum Running sum of candidate weights.
 * @param M Number of candidates seen so far.
 */
public struct ReservoirAccum
{
    public float wSum;
    public int32_t M;
}

public ReservoirAccum ReservoirAccumEmpty()
{
    ReservoirAccum a;
    a.wSum = 0.0;
    a.M = 0;
    return a;
}

/**
 * Streaming WRS update for a single new candidate.
 *
 * @param accum Running accumulator.
 * @param r Reservoir to update on selection.
 * @param candidateWeight w_i = targetPdf(x) / sourcePdf(x).
 * @param u Uniform random float in [0, 1).
 * @return True if this candidate was selected.
 */
public bool ReservoirAddSample(inout ReservoirAccum accum, inout Reservoir r, float candidateWeight, float u)
{
    accum.wSum += candidateWeight;
    accum.M += 1;
    if (u < candidateWeight / accum.wSum)
    {
        return true;
    }
    return false;
}

/**
 * Finalises W after all candidates have been streamed.
 *
 * For the initial generation pass (Talbot MIS), M is already absorbed into each w_i = p_hat/denom,
 * so pass M_total = 1; W = wSum / p_hat(y*).
 *
 * For reuse combination passes (temporal/spatial), pass M_total = float(combined.M);
 * W = wSum / (M_total * p_hat(y*)).
 *
 * @param r Reservoir to finalise.
 * @param accum Completed accumulator.
 * @param p_hat_yStar p_hat evaluated at the selected sample y*.
 * @param M_total Total candidate count across all merged reservoirs.
 */
public void ReservoirFinalise(inout Reservoir r, ReservoirAccum accum, float p_hat_yStar, float M_total)
{
    if (p_hat_yStar <= 0.0 || M_total <= 0.0) {
        r.W = 0.0;
    } else {
        r.W = accum.wSum / (M_total * p_hat_yStar);
    }
}

/**
 * Evaluates the full BRDF integrand as a scalar luminance for use as p_hat.
 * Includes the area-light geometry term (cosLight / dist²), attenuation, and light emission.
 * Returns 0 if cosShade, cosLight, or attenuation are non-positive.
 *
 * @param s Area light sample (geometry terms, must have cosShade > 0, cosLight > 0, attenuation > 0).
 * @param V View direction (toward camera, view space).
 * @param N Shading normal (view space).
 * @param albedo Diffuse albedo.
 * @param F0 Specular F0.
 * @param metallic Metallic factor.
 * @param roughness Perceptual roughness (clamped before calling).
 * @param lightColor Decoded linear light color.
 * @param lightIntensity Light intensity scalar.
 */
public float EvalPHat(AreaLightSample s, float3 V, float3 N, float3 albedo, float3 F0, float metallic, float roughness, float3 lightColor, float lightIntensity)
{
    if (s.cosShade <= 0.0 || s.cosLight <= 0.0 || s.attenuation <= 0.0) { return 0.0; }
    float3 H = normalize(V + s.L);
    float NDF = D_GGX(N, H, roughness);
    float G = G_SMITH_HEIGHT_CORRELATED(N, V, s.L, roughness);
    float3 F = F_SCHLICK(V, H, F0);
    float3 specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * s.cosShade, 0.001);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float geomTerm = s.cosLight / max(s.dist * s.dist, 0.0001);
    float3 brdfCos = (kD * albedo / PI + specular) * s.cosShade * geomTerm * lightIntensity * lightColor * s.attenuation;
    return dot(brdfCos, float3(0.2126, 0.7152, 0.0722));
}

/**
 * EvalPHat with a shadow ray visibility test. Returns 0 if the sample is occluded.
 * worldShadingPos and worldN must be in world space; worldyStar is the light sample in world space.
 * The caller is responsible for fetching the AS handle from the bindless array.
 *
 * @param tlas Top-level acceleration structure.
 * @param worldShadingPos Shading point in world space.
 * @param worldN Shading normal in world space (used for ray origin bias).
 * @param worldyStar Light sample position in world space.
 */
[ForceInline]
public float EvalPHatVisibility(AreaLightSample s, float3 V, float3 N, float3 albedo, float3 F0, float metallic, float roughness, float3 lightColor, float lightIntensity,
                                 RaytracingAccelerationStructure tlas, float3 worldShadingPos, float3 worldN, float3 worldyStar)
{
    float3 toLight = worldyStar - worldShadingPos;
    float lightDist = length(toLight);
    RayDesc shadowRay;
    shadowRay.Origin = worldShadingPos + worldN * 0.001;
    shadowRay.Direction = toLight / lightDist;
    shadowRay.TMin = 0.1;
    shadowRay.TMax = lightDist - 0.01;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    q.TraceRayInline(tlas, 0, 0xFF, shadowRay);
    q.Proceed();
    if (q.CommittedStatus() != COMMITTED_NOTHING) { return 0.0; }
    return EvalPHat(s, V, N, albedo, F0, metallic, roughness, lightColor, lightIntensity);
}

/**
 * Evaluates the GGX solid-angle PDF for a given light direction L.
 * Matches the distribution sampled by ImportanceSampleGGX.
 *
 * @param N Shading normal.
 * @param V View direction (toward camera).
 * @param L Light direction to evaluate.
 * @param roughness Perceptual roughness (squared internally).
 * @return pdf >= 0. Returns 0 if L is below the hemisphere or H is degenerate.
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
 * @param rayOrigin Ray origin.
 * @param rayDir Ray direction (need not be normalised).
 * @param center Quad center.
 * @param normal Quad normal (must face the side the ray comes from for a valid hit).
 * @param right Quad right axis (unit length).
 * @param up Quad up axis (unit length).
 * @param halfWidth Half-extent along right.
 * @param halfHeight Half-extent along up.
 * @param t Output: ray parameter at intersection. Negative if miss or behind origin.
 * @return True if the ray hits the quad at t > 0.
 */
public bool RayIntersectQuad(float3 rayOrigin, float3 rayDir, float3 center, float3 normal, float3 right, float3 up, float halfWidth, float halfHeight, out float t)
{
    float denom = dot(normal, rayDir);
    if (denom >= 0.0) { t = -1.0; return false; }
    t = dot(normal, center - rayOrigin) / denom;
    if (t <= 0.0) { return false; }
    float3 hitPos = rayOrigin + t * rayDir;
    float3 local = hitPos - center;
    return abs(dot(local, right)) <= halfWidth && abs(dot(local, up)) <= halfHeight;
}
