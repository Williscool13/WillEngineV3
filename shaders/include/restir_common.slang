module restir_common;

import shader_constants;
import lights_interop;
import restir_interop;
import pbr_functions;
import bindless_declarations;
import constants_interop;
import shader_functions;

// Relative view-depth spread across a 2x2 full-res quad above which it is treated as a silhouette. Must match QUAD_UNIFORM_DEPTH_THRESHOLD in relax_utils.slang.
public static const float QUAD_UNIFORM_DEPTH_THRESHOLD = 0.02;

// Minimum dot between a quad corner's normal and corner 0 below which the quad is treated as a normal crease (and the corner locks, like a silhouette). 0.85 ~= 32deg. -1.0 disables (reverts to depth-only). Must match relax_utils.slang.
public static const float QUAD_UNIFORM_NORMAL_DOT_THRESHOLD = 0.85;

/** Light (area or sphere) transformed into view space, with derived geometry and emission cached. For a sphere, halfWidth holds the radius and normal/right/up/halfHeight are unused. */
public struct LightVS
{
    public float3 center;
    public float3 normal;
    public float3 right;
    public float3 up;
    public float halfWidth; // radius for spheres
    public float halfHeight;
    public float area; // 4*halfWidth*halfHeight (area) or 4*pi*r^2 (sphere)
    public float range;
    public float intensity;
    public uint packedColor;
    public uint type;
}

/**
 * Transforms a LightInfo into view space, branching on light type.
 *
 * @param al World-space light.
 * @param view View matrix (float4x4).
 */
public LightVS TransformLight(LightInfo al, float4x4 view)
{
    LightVS vs;
    vs.center = mul(view, float4(al.position.xyz, 1.0)).xyz;
    vs.range = al.range;
    vs.intensity = al.intensity;
    vs.packedColor = al.packedColor;
    vs.type = al.type;
    if (al.type == LIGHT_TYPE_SPHERE)
    {
        float radius = al.right.w;
        // World axes carried into view space: a stable (camera-independent across frames) basis the sample offset is encoded in, so temporal reuse stays frame-coherent.
        vs.right = normalize(mul((float3x3)view, float3(1.0, 0.0, 0.0)));
        vs.up = normalize(mul((float3x3)view, float3(0.0, 1.0, 0.0)));
        vs.normal = normalize(mul((float3x3)view, float3(0.0, 0.0, 1.0)));
        vs.halfWidth = radius;
        vs.halfHeight = radius;
        vs.area = 4.0 * PI * radius * radius;
    }
    else
    {
        vs.normal = normalize(mul((float3x3)view, al.normal.xyz));
        vs.right = normalize(mul((float3x3)view, al.right.xyz));
        vs.up = normalize(mul((float3x3)view, al.up.xyz));
        vs.halfWidth = al.right.w;
        vs.halfHeight = al.up.w;
        vs.area = 4.0 * vs.halfWidth * vs.halfHeight;
    }
    return vs;
}

/** Packs a LightVS into the GPU buffer layout written by the transform-lights pass. */
public LightVSData PackLightVS(LightVS vs)
{
    LightVSData d;
    d.centerHalfWidth = float4(vs.center, vs.halfWidth);
    d.normalHalfHeight = float4(vs.normal, vs.halfHeight);
    d.rightArea = float4(vs.right, vs.area);
    d.upRange = float4(vs.up, vs.range);
    d.packedColor = vs.packedColor;
    d.intensity = vs.intensity;
    d.type = vs.type;
    d._pad1 = 0.0;
    return d;
}

/** Loads a pre-transformed view-space light from the buffer written by the transform-lights pass. */
public LightVS UnpackLightVS(LightVSData d)
{
    LightVS vs;
    vs.center = d.centerHalfWidth.xyz;
    vs.halfWidth = d.centerHalfWidth.w;
    vs.normal = d.normalHalfHeight.xyz;
    vs.halfHeight = d.normalHalfHeight.w;
    vs.right = d.rightArea.xyz;
    vs.area = d.rightArea.w;
    vs.up = d.upRange.xyz;
    vs.range = d.upRange.w;
    vs.packedColor = d.packedColor;
    vs.intensity = d.intensity;
    vs.type = d.type;
    return vs;
}

/** Geometric quantities for a point on a light as seen from a shading point. */
public struct LightSample
{
    public float3 samplePos;
    public float3 L;
    public float dist;
    public float cosShade;
    public float cosLight;
    public float attenuation;
}

/**
 * Evaluates geometry for a specific sample position on a light.
 *
 * @param vs View-space light.
 * @param samplePos Point on the light surface (view space).
 * @param shadingPos Shading point (view space).
 * @param N Shading normal.
 * @param range Light range for attenuation falloff.
 */
public LightSample EvalLightSample(LightVS vs, float3 samplePos, float3 shadingPos, float3 N, float range)
{
    LightSample s;
    s.samplePos = samplePos;
    float3 toSample = samplePos - shadingPos;
    s.dist = max(length(toSample), 1e-5);
    s.L = toSample / s.dist;
    s.cosShade = max(dot(N, s.L), 0.0);
    if (vs.type == LIGHT_TYPE_SPHERE)
    {
        float3 outwardNormal = normalize(samplePos - vs.center);
        s.cosLight = max(dot(outwardNormal, -s.L), 0.0);
    }
    else
    {
        s.cosLight = max(dot(-vs.normal, s.L), 0.0);
    }

    s.attenuation = LightRangeCull(s.dist, range);
    return s;
}

/** LightSample overload of pbr_functions::LightGeometryTerm. */
public float LightGeometryTerm(LightSample s)
{
    return LightGeometryTerm(s.cosLight, s.dist);
}

/** Uniformly samples a unit direction on the sphere. */
public float3 UniformSampleSphere(float u1, float u2)
{
    float z = 1.0 - 2.0 * u1;
    float r = sqrt(max(1.0 - z * z, 0.0));
    float phi = 2.0 * PI * u2;
    return float3(r * cos(phi), r * sin(phi), z);
}

/**
 * Returns a random point on the light surface.
 *
 * @param vs View-space light.
 * @param uU Uniform random in [0, 1) along right axis (area) / sphere parameter.
 * @param uV Uniform random in [0, 1) along up axis (area) / sphere parameter.
 */
public float3 SampleLightPos(LightVS vs, float uU, float uV)
{
    if (vs.type == LIGHT_TYPE_SPHERE)
    {
        return vs.center + vs.halfWidth * UniformSampleSphere(uU, uV);
    }
    return vs.center + (uU * 2.0 - 1.0) * vs.halfWidth * vs.right + (uV * 2.0 - 1.0) * vs.halfHeight * vs.up;
}

/**
 * 1 - cosThetaMax of the cone a sphere subtends from shadingPos, computed without catastrophic cancellation.
 * The naive 1 - sqrt(1 - sin2) loses all precision for small/distant spheres (sqrt rounds to 1.0), which then
 * collides with the PDF floor and inflates sphere sample weights into persistent bright spots. This form is
 * algebraically identical but stable: the numerator sin2ThetaMax is exact and the denominator is ~2.
 * Shared by SampleLightSurface and LightSamplePdfArea so the sampled cone and the reported PDF use one value.
 *
 * @param d2 Squared distance from shadingPos to the sphere center.
 * @param radius Sphere radius.
 */
public float SphereConeOneMinusCosThetaMax(float d2, float radius)
{
    float sin2ThetaMax = clamp(radius * radius / d2, 0.0, 0.9999);
    return sin2ThetaMax / (1.0 + sqrt(max(1.0 - sin2ThetaMax, 0.0)));
}

/**
 * Samples a point on the light for shading. Spheres use cone (solid-angle) sampling toward the subtended visible cap
 * (no wasted back-hemisphere samples); rects use uniform area. Pair with LightSamplePdfArea for the source PDF.
 */
public float3 SampleLightSurface(LightVS vs, float3 shadingPos, float uU, float uV)
{
    if (vs.type == LIGHT_TYPE_SPHERE)
    {
        float3 toCenter = vs.center - shadingPos;
        float d2 = max(dot(toCenter, toCenter), 1e-8);
        float d = sqrt(d2);
        float radius = vs.halfWidth;
        float oneMinusCosThetaMax = SphereConeOneMinusCosThetaMax(d2, radius);
        float cosTheta = 1.0 - uU * oneMinusCosThetaMax;
        float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
        float phi = 2.0 * PI * uV;
        float3 w = toCenter / d;
        float3 up = abs(w.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
        float3 t = normalize(cross(up, w));
        float3 b = cross(w, t);
        float3 dir = sinTheta * cos(phi) * t + sinTheta * sin(phi) * b + cosTheta * w;
        float3 oc = shadingPos - vs.center;
        float bq = dot(oc, dir);
        float disc = max(bq * bq - (d2 - radius * radius), 0.0);
        float s = -bq - sqrt(disc);
        return shadingPos + max(s, 1e-4) * dir;
    }
    return vs.center + (uU * 2.0 - 1.0) * vs.halfWidth * vs.right + (uV * 2.0 - 1.0) * vs.halfHeight * vs.up;
}

/** Source PDF (area measure) of SampleLightSurface at a point: cone solid-angle (sphere) or uniform 1/area (rect). */
public float LightSamplePdfArea(LightVS vs, float3 shadingPos, float3 samplePos)
{
    if (vs.type == LIGHT_TYPE_SPHERE)
    {
        float3 toCenter = vs.center - shadingPos;
        float d2 = max(dot(toCenter, toCenter), 1e-8);
        float radius = vs.halfWidth;
        float oneMinusCosThetaMax = SphereConeOneMinusCosThetaMax(d2, radius);
        float pdfSolidAngle = 1.0 / (2.0 * PI * max(oneMinusCosThetaMax, 1e-12));
        float3 toSample = samplePos - shadingPos;
        float dist2 = max(dot(toSample, toSample), 1e-8);
        float3 L = toSample * rsqrt(dist2);
        float3 outwardN = normalize(samplePos - vs.center);
        float cosLight = max(dot(outwardN, -L), 0.0);
        return pdfSolidAngle * cosLight / dist2;
    }
    return 1.0 / vs.area;
}

public int3 ReGIRGridDim() { return int3(int(REGIR_GRID_DIM_X), int(REGIR_GRID_DIM_Y), int(REGIR_GRID_DIM_Z)); }
public float3 ReGIRCellSize() { return float3(REGIR_CELL_SIZE_X, REGIR_CELL_SIZE_Y, REGIR_CELL_SIZE_Z); }

public bool ReGIRWorldToCellIndex(float3 worldPos, float3 gridOffset, out uint cellIndex)
{
    int3 dim = ReGIRGridDim();
    float3 cellSize = ReGIRCellSize();
    float3 gridHalf = 0.5 * float3(dim) * cellSize;
    int3 c = int3(floor((worldPos - gridOffset + gridHalf) / cellSize));
    if (any(c < int3(0, 0, 0)) || any(c >= dim))
    {
        cellIndex = ~0u;
        return false;
    }
    cellIndex = (uint(c.z) * REGIR_GRID_DIM_Y + uint(c.y)) * REGIR_GRID_DIM_X + uint(c.x);
    return true;
}

public float3 ReGIRCellCenter(uint cellIndex, float3 gridOffset)
{
    uint cx = cellIndex % REGIR_GRID_DIM_X;
    uint cy = (cellIndex / REGIR_GRID_DIM_X) % REGIR_GRID_DIM_Y;
    uint cz = cellIndex / (REGIR_GRID_DIM_X * REGIR_GRID_DIM_Y);
    float3 cellSize = ReGIRCellSize();
    float3 gridHalf = 0.5 * float3(ReGIRGridDim()) * cellSize;
    return (float3(float(cx), float(cy), float(cz)) + 0.5) * cellSize - gridHalf + gridOffset;
}

/**
 * ReGIR cell-fill target. Uses the SAME (unfloored) geometry term as the shading target EvalPHat.
 * A distance floor here would break the cone-sampling cancellation in both the fill and the in-grid
 * shading RIS: the build target is the denominator of w_i = p_hat / (targetPdf / totalWeight), so a
 * floor caps the denominator for near samples and inflates w_i by minDist2/d² (a sphere-near-cap spike
 * that recirculates through the grid and persists). Genuine near-field spikes are bounded by ReGIRClampW.
 */
public float EvalCellTarget(LightSample s, float3 lightColor, float lightIntensity)
{
    if (s.cosLight <= 0.0 || s.attenuation <= 0.0) { return 0.0; }
    const float3 lumWeights = float3(0.2126, 0.7152, 0.0722);
    return LightGeometryTerm(s) * lightIntensity * dot(lightColor, lumWeights) * s.attenuation;
}

// Half-res pixel -> full-res pixel via the precomputed quad-selection texture (sel in [0,3]). Identity at full res.
public uint2 FetchFullPixel(uint selectionIndex, uint2 pixel, uint pixelScale)
{
    if (pixelScale == 1u) { return pixel; }
    uint sel = rdgTexturesUInt[selectionIndex].Load(int3(int2(pixel), 0)).r;
    return pixel * pixelScale + uint2(sel & 1u, (sel >> 1u) & 1u);
}

public int2 FetchFullPixel(uint selectionIndex, int2 pixel, uint pixelScale)
{
    if (pixelScale == 1u) { return pixel; }
    uint sel = rdgTexturesUInt[selectionIndex].Load(int3(pixel, 0)).r;
    return pixel * int(pixelScale) + int2(int(sel & 1u), int((sel >> 1u) & 1u));
}

// RTXDI permutation sampling: reflects the history pixel within a 4x4 block per frame to decorrelate temporal reuse. Permuted pixel must still pass disocclusion.
public int2 ApplyPermutationSampling(int2 prevPixel, uint frameIndex)
{
    uint seed = frameIndex * 0x9E3779B9u;
    seed ^= seed >> 16u;
    int2 offset = int2(seed & 3u, (seed >> 2u) & 3u);
    prevPixel += offset;
    prevPixel.x ^= 3;
    prevPixel.y ^= 3;
    prevPixel -= offset;
    return prevPixel;
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

public ReGIRReservoir ReGIRReservoirEmpty()
{
    ReGIRReservoir r;
    r.sampleOffsetPacked = 0u;
    r.lightIdx = ~0u;
    r.totalWeight = 0.0;
    r.targetPdf = 0.0;
    return r;
}

/**
 * Finalises a ReGIR grid reservoir into the (totalWeight, targetPdf) form (no pre-divided W).
 * totalWeight = wSum / M_total; targetPdf = the survivor's build target. The shading-side RIS
 * later forms sourcePdf = targetPdf / totalWeight (per-reservoir W computed at shading, not baked per slot).
 */
public void FinalizeReservoirReGIR(inout ReGIRReservoir r, ReservoirAccum accum, float targetPdf, float M_total)
{
    r.targetPdf = targetPdf;
    if (targetPdf <= 0.0 || M_total <= 0.0) {
        r.totalWeight = 0.0;
    } else {
        r.totalWeight = accum.wSum / M_total;
    }
}

/** Effective unbiased weight W = totalWeight / targetPdf of a grid reservoir; used when merging history grids. */
public float ReGIRReservoirW(ReGIRReservoir r)
{
    return (r.targetPdf > 0.0) ? (r.totalWeight / r.targetPdf) : 0.0;
}

/** Clamps the effective weight (totalWeight/targetPdf) to wClamp by capping totalWeight; wClamp <= 0 disables. */
public void ReGIRClampW(inout ReGIRReservoir r, float wClamp)
{
    if (wClamp > 0.0 && r.targetPdf > 0.0) { r.totalWeight = min(r.totalWeight, wClamp * r.targetPdf); }
}

/**
 * Encodes a view-space sample position into a uint. For an area light, stores snorm16x2 offsets
 * in the right/up basis; for a sphere, stores the oct16-packed outward unit direction from center.
 */
public uint EncodeSampleOffset(float3 samplePos, LightVS vs)
{
    if (vs.type == LIGHT_TYPE_SPHERE)
    {
        float3 dir = normalize(samplePos - vs.center);
        float3 localDir = float3(dot(dir, vs.right), dot(dir, vs.up), dot(dir, vs.normal));
        return PackNormalOct16(localDir);
    }
    float3 delta = samplePos - vs.center;
    float u = clamp(dot(delta, vs.right) / vs.halfWidth, -1.0, 1.0);
    float v = clamp(dot(delta, vs.up) / vs.halfHeight, -1.0, 1.0);
    uint ux = uint(int(u * 32767.0) & 0xFFFF);
    uint vy = uint(int(v * 32767.0) & 0xFFFF);
    return ux | (vy << 16);
}

/**
 * Reconstructs the view-space sample position from a packed sample offset.
 */
public float3 DecodeSamplePos(uint sampleOffsetPacked, LightVS vs)
{
    if (vs.type == LIGHT_TYPE_SPHERE)
    {
        float3 localDir = UnpackNormalOct16(sampleOffsetPacked);
        float3 dir = localDir.x * vs.right + localDir.y * vs.up + localDir.z * vs.normal;
        return vs.center + vs.halfWidth * dir;
    }
    int ix = int(sampleOffsetPacked << 16) >> 16;
    int iy = int(sampleOffsetPacked) >> 16;
    float u = max(float(ix) / 32767.0, -1.0);
    float v = max(float(iy) / 32767.0, -1.0);
    return vs.center + u * vs.halfWidth * vs.right + v * vs.halfHeight * vs.up;
}

public float3 DecodeSamplePos(Reservoir r, LightVS vs)
{
    return DecodeSamplePos(r.sampleOffsetPacked, vs);
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
 * Streaming WRS update for a single new candidate. Returns whether the candidate was selected; the
 * caller writes the survivor's fields into its reservoir on a true return.
 *
 * @param accum Running accumulator.
 * @param candidateWeight w_i = targetPdf(x) / sourcePdf(x).
 * @param u Uniform random float in [0, 1).
 * @return True if this candidate was selected.
 */
public bool ReservoirAddSample(inout ReservoirAccum accum, float candidateWeight, float u)
{
    accum.wSum += candidateWeight;
    accum.M += 1;
    return u < candidateWeight / accum.wSum;
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
 * Cheap scalar target function for reservoir sampling: Lambert diffuse plus a GGX NDF specular approximation
 * (Smith G and Fresnel dropped, G / (4 NoV NoL) approximated as 0.25). Includes the area-light geometry term
 * (cosLight / dist²), attenuation, and light emission. p_hat only steers sampling; any mismatch with the
 * resolve BRDF costs variance, never bias. Returns 0 if cosShade, cosLight, or attenuation are non-positive.
 *
 * @param s Light sample (geometry terms, must have cosShade > 0, cosLight > 0, attenuation > 0).
 * @param V View direction (toward camera, view space).
 * @param N Shading normal (view space).
 * @param albedo Diffuse albedo.
 * @param F0 Specular F0.
 * @param metallic Metallic factor.
 * @param roughness Perceptual roughness (clamped before calling).
 * @param lightColor Decoded linear light color.
 * @param lightIntensity Light intensity scalar.
 */
public float EvalPHat(LightSample s, float3 V, float3 N, float3 albedo, float3 F0, float metallic, float roughness, float3 lightColor, float lightIntensity)
{
    if (s.cosShade <= 0.0 || s.cosLight <= 0.0 || s.attenuation <= 0.0) { return 0.0; }
    const float3 lumWeights = float3(0.2126, 0.7152, 0.0722);
    float3 H = normalize(V + s.L);
    float D = D_GGX(N, H, roughness);
    float brdf = (1.0 - metallic) * dot(albedo, lumWeights) / PI + 0.25 * dot(F0, lumWeights) * D;
    // Uniform color weight, not luminance: a luminance-weighted target under-samples low-luminance hues (red vs yellow ~4x), which W must compensate with sparse high-energy spikes.
    //return brdf * s.cosShade * LightGeometryTerm(s) * lightIntensity * dot(lightColor, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0)) * s.attenuation;
    return brdf * s.cosShade * LightGeometryTerm(s) * lightIntensity * dot(lightColor, lumWeights) * s.attenuation;

    // Full BRDF target (GGX + Smith + Fresnel), kept for reference:
    // float3 H = normalize(V + s.L);
    // float NDF = D_GGX(N, H, roughness);
    // float G = G_SMITH_HEIGHT_CORRELATED(N, V, s.L, roughness);
    // float3 F = F_SCHLICK(V, H, F0);
    // float3 specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * s.cosShade, 0.001);
    // float3 kD = (1.0 - F) * (1.0 - metallic);
    // float geomTerm = s.cosLight / max(s.dist * s.dist, 0.0001);
    // float3 brdfCos = (kD * albedo / PI + specular) * s.cosShade * geomTerm * lightIntensity * lightColor * s.attenuation;
    // return dot(brdfCos, float3(0.2126, 0.7152, 0.0722));
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
/**
 * Binary shadow-ray test to a light sample. Returns true if the sample is visible from the shading point.
 * Mask 0x01 = scene geometry only (light proxy meshes are skipped). worldN biases the origin so the ray doesn't self-intersect; TMax is measured from the offset origin so the bias can't eat the far-end margin.
 */
public bool RestirShadowVisible(RaytracingAccelerationStructure tlas, float3 worldShadingPos, float3 worldN, float3 worldyStar)
{
    float3 rayOrigin = worldShadingPos + worldN * 0.05;
    float3 toLight = worldyStar - rayOrigin;
    float lightDist = length(toLight);
    RayDesc shadowRay;
    shadowRay.Origin = rayOrigin;
    shadowRay.Direction = toLight / lightDist;
    shadowRay.TMin = 0.001;
    shadowRay.TMax = lightDist - 0.05;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    q.TraceRayInline(tlas, 0, 0x01, shadowRay);
    q.Proceed();
    return q.CommittedStatus() == COMMITTED_NOTHING;
}

[ForceInline]
public float EvalPHatVisibility(LightSample s, float3 V, float3 N, float3 albedo, float3 F0, float metallic, float roughness, float3 lightColor, float lightIntensity,
                                 RaytracingAccelerationStructure tlas, float3 worldShadingPos, float3 worldN, float3 worldyStar)
{
    if (!RestirShadowVisible(tlas, worldShadingPos, worldN, worldyStar)) { return 0.0; }
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

/**
 * Ray vs. light intersection, branching on light type. Sphere returns the nearest positive root.
 * rayDir must be unit length for the sphere path.
 *
 * @param vs View-space light.
 * @param rayOrigin Ray origin (view space).
 * @param rayDir Ray direction (unit length).
 * @param t Output: ray parameter at the surface hit. Negative on miss.
 * @return True on a hit at t > 0.
 */
public bool RayIntersectLight(LightVS vs, float3 rayOrigin, float3 rayDir, out float t)
{
    if (vs.type == LIGHT_TYPE_SPHERE)
    {
        float3 oc = rayOrigin - vs.center;
        float b = dot(oc, rayDir);
        float c = dot(oc, oc) - vs.halfWidth * vs.halfWidth;
        float disc = b * b - c;
        if (disc < 0.0) { t = -1.0; return false; }
        float sq = sqrt(disc);
        t = -b - sq;
        if (t <= 0.0) { t = -b + sq; }
        return t > 0.0;
    }
    return RayIntersectQuad(rayOrigin, rayDir, vs.center, vs.normal, vs.right, vs.up, vs.halfWidth, vs.halfHeight, t);
}
