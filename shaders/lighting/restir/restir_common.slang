module restir_common;

import shader_constants;
import lights_interop;
import restir_interop;
import instance_mask_interop;
import rt_alpha_test_common;
import pbr_functions;
import bindless_declarations;
import constants_interop;
import shader_functions;
import packing_functions;

// Fixes ridiculous looking specular light DI
public static const float RESTIR_MIN_ROUGHNESS = 0.045;

public static const float QUAD_UNIFORM_DEPTH_THRESHOLD = 0.02;

public static const float QUAD_UNIFORM_NORMAL_DOT_THRESHOLD = 0.85;

// field: 0 = full-res, 1/2 = active checkerboard parity; reservoir buffers are half-width when field != 0
public uint RestirCheckerboardStride(uint width, uint field)
{
    return (field != 0u) ? ((width + 1u) >> 1u) : width;
}

public uint2 ReservoirPosToPixelPos(uint2 reservoirPos, uint field)
{
    if (field == 0u) { return reservoirPos; }
    return uint2((reservoirPos.x << 1u) + ((reservoirPos.y + field) & 1u), reservoirPos.y);
}

public uint2 PixelPosToReservoirPos(uint2 pixel, uint field)
{
    if (field == 0u) { return pixel; }
    return uint2(pixel.x >> 1u, pixel.y);
}

public uint ReservoirIndex(uint2 reservoirPos, uint width, uint field)
{
    return reservoirPos.y * RestirCheckerboardStride(width, field) + reservoirPos.x;
}

public bool IsActiveCheckerboardPixel(int2 pixel, bool previousFrame, uint field)
{
    if (field == 0u) { return true; }
    return ((pixel.x + pixel.y + (previousFrame ? 1 : 0)) & 1) == int(field & 1u);
}

public int2 ActivateCheckerboardPixel(int2 pixel, bool previousFrame, uint field)
{
    if (field == 0u || IsActiveCheckerboardPixel(pixel, previousFrame, field)) { return pixel; }
    if (previousFrame) { pixel.x += int(field) * 2 - 3; }
    else { pixel.x += ((pixel.y & 1) != 0) ? 1 : -1; }
    return pixel;
}

// Horizontal neighbor pixel belonging to the opposite checkerboard field
public int2 OppositeFieldPixel(uint2 pixel, uint field)
{
    int2 p = int2(pixel);
    p.x += ((field == 1u) == ((pixel.y & 1u) != 0u)) ? 1 : -1;
    return p;
}

public struct LightView
{
    public float3 center;
    public float3 normal;
    public float3 right;
    public float3 up;
    public float halfWidth;
    public float halfHeight;
    public float area;
    public float range;
    public float intensity;
    public uint packedColor;
    public uint type;
}

public LightView TransformLightToView(LightInfo al, float4x4 view)
{
    LightView vs;
    vs.center = mul(view, float4(al.position.xyz, 1.0)).xyz;
    vs.range = al.range;
    vs.intensity = al.intensity;
    vs.packedColor = al.packedColor;
    vs.type = al.type;
    if (al.type == LIGHT_TYPE_SPHERE)
    {
        float radius = al.right.w;
        vs.right = normalize(mul((float3x3)view, float3(1.0, 0.0, 0.0)));
        vs.up = normalize(mul((float3x3)view, float3(0.0, 1.0, 0.0)));
        vs.normal = normalize(mul((float3x3)view, float3(0.0, 0.0, 1.0)));
        vs.halfWidth = radius;
        vs.halfHeight = radius;
        vs.area = 4.0 * PI * radius * radius;
    }
    else if (al.type == LIGHT_TYPE_TRIANGLE)
    {
        // right/up are the triangle edges e1/e2; must stay unnormalized so the barycentric param maps to the surface
        vs.normal = normalize(mul((float3x3)view, al.normal.xyz));
        vs.right = mul((float3x3)view, al.right.xyz);
        vs.up = mul((float3x3)view, al.up.xyz);
        vs.halfWidth = 0.0;
        vs.halfHeight = 0.0;
        vs.area = 0.5 * length(cross(vs.right, vs.up));
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

public LightVSData PackLightVS(LightView vs)
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

public LightView UnpackLightVS(LightVSData d)
{
    LightView vs;
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

public struct LightSample
{
    public float3 samplePos;
    public float3 L;
    public float dist;
    public float cosShade;
    public float cosLight;
    public float attenuation;
}

public LightSample EvalLightSample(LightView vs, float3 samplePos, float3 shadingPos, float3 N, float range)
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

public float LightGeometryTerm(LightSample s)
{
    return LightGeometryTerm(s.cosLight, s.dist);
}

public float3 UniformSampleSphere(float u1, float u2)
{
    float z = 1.0 - 2.0 * u1;
    float r = sqrt(max(1.0 - z * z, 0.0));
    float phi = 2.0 * PI * u2;
    return float3(r * cos(phi), r * sin(phi), z);
}

public float3 SampleLightPos(LightView vs, float uU, float uV)
{
    if (vs.type == LIGHT_TYPE_SPHERE)
    {
        return vs.center + vs.halfWidth * UniformSampleSphere(uU, uV);
    }
    if (vs.type == LIGHT_TYPE_TRIANGLE)
    {
        float su = sqrt(uU);
        float b1 = 1.0 - su;
        float b2 = uV * su;
        return vs.center + b1 * vs.right + b2 * vs.up;
    }
    return vs.center + (uU * 2.0 - 1.0) * vs.halfWidth * vs.right + (uV * 2.0 - 1.0) * vs.halfHeight * vs.up;
}

public float SphereConeOneMinusCosThetaMax(float d2, float radius)
{
    float sin2ThetaMax = clamp(radius * radius / d2, 0.0, 0.9999);
    return sin2ThetaMax / (1.0 + sqrt(max(1.0 - sin2ThetaMax, 0.0)));
}

static const float QUAD_SOLID_ANGLE_MIN = 1e-6;

struct SphQuad
{
    float3 o;
    float3 x;
    float3 y;
    float3 z;
    float z0;
    float x0;
    float y0;
    float x1;
    float y1;
    float b0;
    float b1;
    float k;
    float S;
}

// Urena et al. 2013, "An Area-Preserving Parametrization for Spherical Rectangles"
SphQuad SphQuadInit(float3 corner, float3 ex, float3 ey, float3 o)
{
    SphQuad sq;
    sq.o = o;
    float exl = length(ex);
    float eyl = length(ey);
    sq.x = ex / exl;
    sq.y = ey / eyl;
    sq.z = cross(sq.x, sq.y);
    float3 d = corner - o;
    sq.z0 = dot(d, sq.z);
    if (sq.z0 > 0.0)
    {
        sq.z = -sq.z;
        sq.z0 = -sq.z0;
    }
    sq.x0 = dot(d, sq.x);
    sq.y0 = dot(d, sq.y);
    sq.x1 = sq.x0 + exl;
    sq.y1 = sq.y0 + eyl;

    float3 v00 = float3(sq.x0, sq.y0, sq.z0);
    float3 v01 = float3(sq.x0, sq.y1, sq.z0);
    float3 v10 = float3(sq.x1, sq.y0, sq.z0);
    float3 v11 = float3(sq.x1, sq.y1, sq.z0);
    float3 n0 = normalize(cross(v00, v10));
    float3 n1 = normalize(cross(v10, v11));
    float3 n2 = normalize(cross(v11, v01));
    float3 n3 = normalize(cross(v01, v00));
    float g0 = acos(clamp(-dot(n0, n1), -1.0, 1.0));
    float g1 = acos(clamp(-dot(n1, n2), -1.0, 1.0));
    float g2 = acos(clamp(-dot(n2, n3), -1.0, 1.0));
    float g3 = acos(clamp(-dot(n3, n0), -1.0, 1.0));
    sq.b0 = n0.z;
    sq.b1 = n2.z;
    sq.k = 2.0 * PI - g2 - g3;
    sq.S = g0 + g1 - sq.k;
    return sq;
}

float3 SphQuadSample(SphQuad sq, float u, float v)
{
    float au = u * sq.S + sq.k;
    // sin(au) is legitimately negative over most of the u range (k ~ pi for frontal quads); the epsilon guard must keep its sign or the u dimension collapses
    float sinAu = sin(au);
    float fu = (cos(au) * sq.b0 - sq.b1) / (abs(sinAu) > 1e-7 ? sinAu : (sinAu >= 0.0 ? 1e-7 : -1e-7));
    float cu = (fu > 0.0 ? 1.0 : -1.0) / sqrt(fu * fu + sq.b0 * sq.b0);
    cu = clamp(cu, -1.0, 1.0);

    float xu = -(cu * sq.z0) / max(sqrt(1.0 - cu * cu), 1e-7);
    xu = clamp(xu, sq.x0, sq.x1);

    float d = sqrt(xu * xu + sq.z0 * sq.z0);
    float h0 = sq.y0 / sqrt(d * d + sq.y0 * sq.y0);
    float h1 = sq.y1 / sqrt(d * d + sq.y1 * sq.y1);
    float hv = h0 + v * (h1 - h0);
    float hv2 = hv * hv;
    float yv = (hv2 < 1.0 - 1e-6) ? (hv * d) / sqrt(1.0 - hv2) : sq.y1;
    yv = clamp(yv, sq.y0, sq.y1);

    return sq.o + xu * sq.x + yv * sq.y + sq.z0 * sq.z;
}

public float3 SampleLightSurface(LightView vs, float3 shadingPos, float uU, float uV)
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
    if (vs.type == LIGHT_TYPE_TRIANGLE)
    {
        float su = sqrt(uU);
        float b1 = 1.0 - su;
        float b2 = uV * su;
        return vs.center + b1 * vs.right + b2 * vs.up;
    }
    float3 ex = (2.0 * vs.halfWidth) * vs.right;
    float3 ey = (2.0 * vs.halfHeight) * vs.up;
    float3 corner = vs.center - vs.halfWidth * vs.right - vs.halfHeight * vs.up;
    SphQuad sq = SphQuadInit(corner, ex, ey, shadingPos);
    if (!(sq.S > QUAD_SOLID_ANGLE_MIN))
    {
        return vs.center + (uU * 2.0 - 1.0) * vs.halfWidth * vs.right + (uV * 2.0 - 1.0) * vs.halfHeight * vs.up;
    }
    return SphQuadSample(sq, uU, uV);
}

public float LightSamplePdfArea(LightView vs, float3 shadingPos, float3 samplePos)
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
    if (vs.type == LIGHT_TYPE_TRIANGLE)
    {
        return 1.0 / vs.area;
    }
    float3 ex = (2.0 * vs.halfWidth) * vs.right;
    float3 ey = (2.0 * vs.halfHeight) * vs.up;
    float3 corner = vs.center - vs.halfWidth * vs.right - vs.halfHeight * vs.up;
    SphQuad sq = SphQuadInit(corner, ex, ey, shadingPos);
    if (!(sq.S > QUAD_SOLID_ANGLE_MIN))
    {
        return 1.0 / vs.area;
    }
    float3 toSample = samplePos - shadingPos;
    float dist2 = max(dot(toSample, toSample), 1e-8);
    float3 L = toSample * rsqrt(dist2);
    float cosLight = max(dot(-vs.normal, L), 0.0);
    return cosLight / (dist2 * sq.S);
}

public float3 ReGIRCellSize() { return float3(REGIR_CELL_SIZE_X, REGIR_CELL_SIZE_Y, REGIR_CELL_SIZE_Z); }

public float EvalCellLightTarget(LightView vs, float3 cellCenter, float3 lightColor)
{
    float3 toCenter = vs.center - cellCenter;
    float dist = max(length(toCenter), 1e-5);
    float atten = LightRangeCull(dist, vs.range);
    if (atten <= 0.0) { return 0.0; }
    float cosFacing = (vs.type == LIGHT_TYPE_SPHERE) ? 1.0 : max(dot(vs.normal, -toCenter / dist), 0.0);
    if (cosFacing <= 0.0) { return 0.0; }
    float colorMax = max(lightColor.r, max(lightColor.g, lightColor.b));
    float geom = cosFacing / max(dist * dist, LIGHT_GEOM_MIN_DIST2);
    return geom * vs.area * vs.intensity * colorMax * atten;
}

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

public void FinalizeReservoirReGIR(inout ReGIRReservoir r, ReservoirAccum accum, float targetPdf, float M_total)
{
    r.targetPdf = targetPdf;
    if (targetPdf <= 0.0 || M_total <= 0.0) {
        r.totalWeight = 0.0;
    } else {
        r.totalWeight = accum.wSum / M_total;
    }
}

public float ReGIRReservoirW(ReGIRReservoir r)
{
    return (r.targetPdf > 0.0) ? (r.totalWeight / r.targetPdf) : 0.0;
}

public void ReGIRClampW(inout ReGIRReservoir r, float wClamp)
{
    if (wClamp > 0.0 && r.targetPdf > 0.0) { r.totalWeight = min(r.totalWeight, wClamp * r.targetPdf); }
}

// 32-bit sample-point packing: sphere = oct16 local direction, triangle = 2x16 unorm barycentrics, quad = 2x snorm16 uv
public uint EncodeSampleOffset(float3 samplePos, LightView vs)
{
    if (vs.type == LIGHT_TYPE_SPHERE)
    {
        float3 dir = normalize(samplePos - vs.center);
        float3 localDir = float3(dot(dir, vs.right), dot(dir, vs.up), dot(dir, vs.normal));
        return PackNormalOct16(localDir);
    }
    if (vs.type == LIGHT_TYPE_TRIANGLE)
    {
        // Solve the 2x2 Gram system for barycentrics (right/up are non-orthogonal edges)
        float3 delta = samplePos - vs.center;
        float a = dot(vs.right, vs.right);
        float b = dot(vs.right, vs.up);
        float c = dot(vs.up, vs.up);
        float d1 = dot(delta, vs.right);
        float d2 = dot(delta, vs.up);
        float det = a * c - b * b;
        float invDet = det > 1e-20 ? 1.0 / det : 0.0;
        float b1 = clamp((d1 * c - d2 * b) * invDet, 0.0, 1.0);
        float b2 = clamp((a * d2 - b * d1) * invDet, 0.0, 1.0);
        uint bx = uint(b1 * 65535.0 + 0.5) & 0xFFFF;
        uint by = uint(b2 * 65535.0 + 0.5) & 0xFFFF;
        return bx | (by << 16);
    }
    float3 delta = samplePos - vs.center;
    float u = clamp(dot(delta, vs.right) / vs.halfWidth, -1.0, 1.0);
    float v = clamp(dot(delta, vs.up) / vs.halfHeight, -1.0, 1.0);
    uint ux = uint(int(u * 32767.0) & 0xFFFF);
    uint vy = uint(int(v * 32767.0) & 0xFFFF);
    return ux | (vy << 16);
}

public float3 DecodeSamplePos(uint sampleOffsetPacked, LightView vs)
{
    if (vs.type == LIGHT_TYPE_SPHERE)
    {
        float3 localDir = UnpackNormalOct16(sampleOffsetPacked);
        float3 dir = localDir.x * vs.right + localDir.y * vs.up + localDir.z * vs.normal;
        return vs.center + vs.halfWidth * dir;
    }
    if (vs.type == LIGHT_TYPE_TRIANGLE)
    {
        float b1 = float(sampleOffsetPacked & 0xFFFF) / 65535.0;
        float b2 = float((sampleOffsetPacked >> 16) & 0xFFFF) / 65535.0;
        return vs.center + b1 * vs.right + b2 * vs.up;
    }
    int ix = int(sampleOffsetPacked << 16) >> 16;
    int iy = int(sampleOffsetPacked) >> 16;
    float u = max(float(ix) / 32767.0, -1.0);
    float v = max(float(iy) / 32767.0, -1.0);
    return vs.center + u * vs.halfWidth * vs.right + v * vs.halfHeight * vs.up;
}

public float3 DecodeSamplePos(Reservoir r, LightView vs)
{
    return DecodeSamplePos(r.sampleOffsetPacked, vs);
}

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

public bool ReservoirAddSample(inout ReservoirAccum accum, float candidateWeight, float u)
{
    accum.wSum += candidateWeight;
    accum.M += 1;
    return u < candidateWeight / accum.wSum;
}

public void ReservoirFinalize(inout Reservoir r, ReservoirAccum accum, float p_hat_yStar, float M_total)
{
    if (p_hat_yStar <= 0.0 || M_total <= 0.0) {
        r.W = 0.0;
    } else {
        r.W = accum.wSum / (M_total * p_hat_yStar);
    }
}

public float EvalPHat(LightSample s, float3 V, float3 N, float3 albedo, float3 F0, float metallic, float roughness, float3 lightColor, float lightIntensity, float lightSpecularFromReflectionsMax)
{
    if (s.cosShade <= 0.0 || s.cosLight <= 0.0 || s.attenuation <= 0.0) { return 0.0; }
    const float3 lumWeights = float3(0.2126, 0.7152, 0.0722);
    float3 H = normalize(V + s.L);

    // Defer specular from local lights to the traced reflection pass across its whole eligible roughness range
    float spec = 0.0;
    if (roughness > lightSpecularFromReflectionsMax) {
        float D = D_GGX(N, H, roughness);
        float G = G_SMITH_HEIGHT_CORRELATED(N, V, s.L, roughness);
        float F = dot(F_SCHLICK(V, H, F0), lumWeights);
        spec = D * G * F / max(4.0 * max(dot(N, V), 0.0) * s.cosShade, 0.001);
    }
    float brdf = (1.0 - metallic) * dot(albedo, lumWeights) / PI + spec;
    // Max-channel (not luminance): must match EvalCellLightTarget, avoids chromatic fireflies from saturated colors.
    float colorMax = max(lightColor.r, max(lightColor.g, lightColor.b));
    return brdf * s.cosShade * LightGeometryTerm(s) * lightIntensity * colorMax * s.attenuation;
}

public bool RestirShadowVisible(RaytracingAccelerationStructure tlas, float3 worldShadingPos, float3 worldN, float3 worldyStar, float worldTexelSize)
{
    float3 rayOrigin = OffsetGBufferRayOrigin(worldShadingPos, worldN, worldTexelSize, 1.0);
    float3 toLight = worldyStar - rayOrigin;
    float lightDist = length(toLight);
    RayDesc shadowRay;
    shadowRay.Origin = rayOrigin;
    shadowRay.Direction = toLight / lightDist;
    shadowRay.TMin = 0.0;
    shadowRay.TMax = max(0.0, lightDist - max(0.002, lightDist * 0.005));
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    q.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, shadowRay);
    q.Proceed();
    return q.CommittedStatus() == COMMITTED_NOTHING;
}

[ForceInline]
public float EvalPHatVisibility(LightSample s, float3 V, float3 N, float3 albedo, float3 F0, float metallic, float roughness, float3 lightColor, float lightIntensity,
                                 RaytracingAccelerationStructure tlas, float3 worldShadingPos, float3 worldN, float3 worldyStar, float worldTexelSize, float lightSpecularFromReflectionsMax)
{
    if (!RestirShadowVisible(tlas, worldShadingPos, worldN, worldyStar, worldTexelSize)) { return 0.0; }
    return EvalPHat(s, V, N, albedo, F0, metallic, roughness, lightColor, lightIntensity, lightSpecularFromReflectionsMax);
}

public float3 SunDirectionWorld(DirectionalLightData sun)
{
    return normalize(-sun.directionIntensity.xyz);
}

// Uniform cone sampling over the sun disk has pdf 1/solidAngle, which cancels the solid-angle divide that
// would turn illuminance into radiance. Both terms are therefore dropped and the shading path uses illuminance directly.
public float3 SunIlluminance(DirectionalLightData sun)
{
    return UnpackUnorm4x8(sun.packedColor).rgb * sun.directionIntensity.w;
}

public float3 SampleSunConeDirection(float3 LsunWorld, float angularRadius, float u1, float u2)
{
    if (angularRadius <= 1e-4) {
        return LsunWorld;
    }
    float cosThetaMax = cos(angularRadius);
    float cosTheta = 1.0 - u1 * (1.0 - cosThetaMax);
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float phi = 2.0 * PI * u2;
    float3 localDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    float3 up = abs(LsunWorld.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tang = normalize(cross(up, LsunWorld));
    float3 bitang = cross(LsunWorld, tang);
    return normalize(localDir.x * tang + localDir.y * bitang + localDir.z * LsunWorld);
}

// Sun visibility target encoding: unorm16x2 cone sample when lit, SUN_VIS_EMPTY when shadowed, facing away or unshaded.
public static const uint SUN_VIS_EMPTY = ~0u;

public uint EncodeSunVis(float2 xi)
{
    // Clamped so a legitimate sample can never quantize onto the sentinel.
    return PackUnorm2x16(min(xi, float2(0.99998, 0.99998)));
}

public float EvalSunPHat(float3 LsunView, float3 V, float3 N, float3 albedo, float3 F0, float metallic, float roughness, float3 sunIlluminance)
{
    float cosShade = max(dot(N, LsunView), 0.0);
    if (cosShade <= 0.0) { return 0.0; }
    const float3 lumWeights = float3(0.2126, 0.7152, 0.0722);
    float3 H = normalize(V + LsunView);
    float D = D_GGX(N, H, roughness);
    float brdf = (1.0 - metallic) * dot(albedo, lumWeights) / PI + 0.25 * dot(F0, lumWeights) * D;
    // Max-channel to match EvalPHat: a tinted sun (sunset) must not inflate the signal into chromatic fireflies.
    float sunMax = max(sunIlluminance.r, max(sunIlluminance.g, sunIlluminance.b));
    return brdf * cosShade * sunMax;
}

public bool RestirSunShadowVisible(RaytracingAccelerationStructure tlas, float3 worldShadingPos, float3 worldN, float3 worldLsun, float worldTexelSize, RTAlphaTestBuffers alphaBuffers, bool bAlphaTest)
{
    // Matches the SIGMA sun trace: footprint-scaled normal offset, no NoL division.
    float3 rayOrigin = OffsetGBufferRayOrigin(worldShadingPos, worldN, worldTexelSize, 1.0);
    RayDesc shadowRay;
    shadowRay.Origin = rayOrigin;
    shadowRay.Direction = worldLsun;
    shadowRay.TMin = 0.0;
    shadowRay.TMax = 1e30;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    q.TraceRayInline(tlas, bAlphaTest ? 0u : RAY_FLAG_FORCE_OPAQUE, INSTANCE_MASK_SUN_OCCLUDER, shadowRay);
    RTProceedAlphaTested(q, alphaBuffers);
    return q.CommittedStatus() == COMMITTED_NOTHING;
}

public float RestirSunShadowBlockerT(RaytracingAccelerationStructure tlas, float3 worldShadingPos, float3 worldN, float3 worldLsun, float worldTexelSize, RTAlphaTestBuffers alphaBuffers, bool bAlphaTest)
{
    // First accepted hit, not nearest (same as the SIGMA trace): an approximate blocker distance is enough.
    float3 rayOrigin = OffsetGBufferRayOrigin(worldShadingPos, worldN, worldTexelSize, 1.0);
    RayDesc shadowRay;
    shadowRay.Origin = rayOrigin;
    shadowRay.Direction = worldLsun;
    shadowRay.TMin = 0.0;
    shadowRay.TMax = 1e30;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    q.TraceRayInline(tlas, bAlphaTest ? 0u : RAY_FLAG_FORCE_OPAQUE, INSTANCE_MASK_SUN_OCCLUDER, shadowRay);
    RTProceedAlphaTested(q, alphaBuffers);
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? q.CommittedRayT() : -1.0;
}

/** Non-sun variant for callers that already unpacked the light; the sun branch, sun argument load, and per-call lightVS element load are hoisted out. */
public float EvalReservoirPHatLight(LightView vs, uint sampleOffsetPacked, float3 viewPos, float3 N, float3 V,
                                    float3 albedo, float3 F0, float metallic, float roughness, float lightSpecularFromReflectionsMax)
{
    float3 yStar = DecodeSamplePos(sampleOffsetPacked, vs);
    LightSample s = EvalLightSample(vs, yStar, viewPos, N, vs.range);
    return EvalPHat(s, V, N, albedo, F0, metallic, roughness, UnpackUnorm4x8(vs.packedColor).rgb, vs.intensity, lightSpecularFromReflectionsMax);
}

public float EvalReservoirPHatVisibilityLight(LightView vs, uint sampleOffsetPacked, float3 viewPos, float3 N, float3 V,
                                              float3 albedo, float3 F0, float metallic, float roughness, float4x4 invView,
                                              RaytracingAccelerationStructure tlas, float3 worldPos, float3 worldN, float worldTexelSize, float lightSpecularFromReflectionsMax)
{
    float3 yStar = DecodeSamplePos(sampleOffsetPacked, vs);
    float3 worldyStar = mul(invView, float4(yStar, 1.0)).xyz;
    if (!RestirShadowVisible(tlas, worldPos, worldN, worldyStar, worldTexelSize)) { return 0.0; }
    LightSample s = EvalLightSample(vs, yStar, viewPos, N, vs.range);
    return EvalPHat(s, V, N, albedo, F0, metallic, roughness, UnpackUnorm4x8(vs.packedColor).rgb, vs.intensity, lightSpecularFromReflectionsMax);
}

static void GGXTangentFrame(float3 N, out float3 T, out float3 B)
{
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

// Bounded VNDF (Eto & Tokuyoshi 2023), isotropic alpha = roughness^2. Tangent-space i = view dir, m = half vector. Returns the solid-angle pdf of the reflected direction.
static float BoundedVNDFPdfTangent(float3 i, float3 m, float a)
{
    float a2 = a * a;
    // Floor must sit below t^2 at min roughness (a2^2). A larger floor underclaims the pdf and over-weights BRDF candidates in MIS.
    float t = m.z * m.z * (a2 - 1.0) + 1.0;
    float ndf = a2 / (PI * max(t * t, 1e-15));
    float2 ai = a * i.xy;
    float len2 = dot(ai, ai);
    float tl = sqrt(len2 + i.z * i.z);
    if (i.z >= 0.0) {
        float s = 1.0 + length(i.xy);
        float s2 = s * s;
        float k = (1.0 - a2) * s2 / (s2 + a2 * i.z * i.z);
        return ndf / (2.0 * (k * i.z + tl));
    }
    return ndf * (tl - i.z) / (2.0 * max(len2, 1e-15));
}

public float GGXPdf(float3 N, float3 V, float3 L, float roughness)
{
    if (dot(N, L) <= 0.0) { return 0.0; }
    float3 T;
    float3 B;
    GGXTangentFrame(N, T, B);
    float3 H = normalize(V + L);
    float3 i = float3(dot(V, T), dot(V, B), dot(V, N));
    float3 m = float3(dot(H, T), dot(H, B), dot(H, N));
    return BoundedVNDFPdfTangent(i, m, roughness * roughness);
}

public float3 ImportanceSampleGGX(float2 xi, float3 N, float3 V, float roughness, out float pdf)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float3 T;
    float3 B;
    GGXTangentFrame(N, T, B);
    float3 i = float3(dot(V, T), dot(V, B), dot(V, N));
    float3 iStd = normalize(float3(i.xy * a, i.z));

    float phi = 2.0 * PI * xi.x;
    float s = 1.0 + length(i.xy);
    float s2 = s * s;
    float k = (1.0 - a2) * s2 / (s2 + a2 * i.z * i.z);
    float b = i.z > 0.0 ? k * iStd.z : iStd.z;
    float z = (1.0 - xi.y) * (1.0 + b) - b;
    float sinTheta = sqrt(saturate(1.0 - z * z));
    float3 oStd = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);

    float3 mStd = iStd + oStd;
    float3 m = normalize(float3(mStd.xy * a, mStd.z));
    float3 H = normalize(T * m.x + B * m.y + N * m.z);
    float3 L = reflect(-V, H);

    if (dot(N, L) <= 0.0) {
        pdf = 0.0;
        return L;
    }
    pdf = BoundedVNDFPdfTangent(i, m, a);
    return L;
}

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

public bool RayIntersectLight(LightView vs, float3 rayOrigin, float3 rayDir, out float t)
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
    if (vs.type == LIGHT_TYPE_TRIANGLE)
    {
        // One-sided Moller-Trumbore over edges right/up (= e1/e2); front side matches the quad convention
        if (dot(vs.normal, rayDir) >= 0.0) { t = -1.0; return false; }
        float3 pvec = cross(rayDir, vs.up);
        float det = dot(vs.right, pvec);
        if (abs(det) < 1e-12) { t = -1.0; return false; }
        float invDet = 1.0 / det;
        float3 tvec = rayOrigin - vs.center;
        float u = dot(tvec, pvec) * invDet;
        if (u < 0.0 || u > 1.0) { t = -1.0; return false; }
        float3 qvec = cross(tvec, vs.right);
        float v = dot(rayDir, qvec) * invDet;
        if (v < 0.0 || u + v > 1.0) { t = -1.0; return false; }
        t = dot(vs.up, qvec) * invDet;
        return t > 0.0;
    }
    return RayIntersectQuad(rayOrigin, rayDir, vs.center, vs.normal, vs.right, vs.up, vs.halfWidth, vs.halfHeight, t);
}
