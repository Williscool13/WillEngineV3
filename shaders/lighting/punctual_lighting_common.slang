module punctual_lighting_common;

import shader_constants;
import lights_interop;
import pbr_functions;
import packing_functions;

/** Area/sphere light degenerated to a view-space punctual source: sphere -> point, rect -> one-sided emitter. */
public struct PunctualLight
{
    public float3 pos; // view-space center
    public float3 dir; // emitter normal (area only)
    public float3 color;
    public float effectiveIntensity; // radiance folded with source area (ReSTIR brightness parity)
    public float radiance;
    public float area;
    public float range; // attenuation cutoff / cluster bounding radius
    public float falloffBias;
    public float sourceRadius; // 0 = delta; >0 drives representative-point specular
    public bool oneSided; // area lights fade to zero across the plane
    public bool bDisk;
    public float cosConeInner; // spot cone on the emitter cosine (area only); 0/0 = hemisphere
    public float cosConeOuter;
    public float3 rightAxis; // rect axes + extents (area only)
    public float3 upAxis;
    public float halfWidth;
    public float halfHeight;
    public bool valid; // false = unsupported light type (e.g. triangle); ShadePunctual no-ops
};

/** World-space area/sphere LightInfo -> view-space PunctualLight. Triangle lights are NOT degenerated: right.w/up.w hold edge-vector components for them, not rect half-extents, so falling through to the rect path would silently misread them. Callers iterating a binned light list should skip LIGHT_TYPE_TRIANGLE entries before calling this (both frustum_binning.slang and world_grid_binning.slang already exclude them from their bins); this valid=false is a defensive backstop, not the primary filter. */
public PunctualLight DegenerateLight(LightInfo light, float4x4 view)
{
    PunctualLight pl;
    pl.pos = mul(view, float4(light.position.xyz, 1.0)).xyz;
    pl.color = UnpackUnorm4x8(light.packedColor).rgb;
    pl.range = light.range;
    pl.falloffBias = light.falloffBias;
    pl.rightAxis = float3(0.0);
    pl.upAxis = float3(0.0);
    pl.halfWidth = 0.0;
    pl.halfHeight = 0.0;
    pl.dir = float3(0.0, 0.0, 1.0);
    pl.effectiveIntensity = 0.0;
    pl.radiance = light.intensity * light.coneScale;
    pl.area = 0.0;
    pl.sourceRadius = 0.0;
    pl.oneSided = false;
    pl.bDisk = false;
    pl.cosConeInner = 0.0;
    pl.cosConeOuter = 0.0;
    pl.valid = true;

    if (light.type == LIGHT_TYPE_SPHERE)
    {
        float radius = light.right.w;
        // Lambertian sphere far-field = point of intensity L * projected area.
        pl.effectiveIntensity = light.intensity * PI * radius * radius;
        pl.sourceRadius = radius;
    }
    else if (light.type == LIGHT_TYPE_AREA || light.type == LIGHT_TYPE_DISK)
    {
        float halfWidth = light.right.w;
        float halfHeight = light.up.w;
        pl.bDisk = light.type == LIGHT_TYPE_DISK;
        pl.dir = normalize(mul((float3x3)view, light.normal.xyz));
        pl.area = pl.bDisk ? PI * halfWidth * halfWidth : 4.0 * halfWidth * halfHeight;
        pl.effectiveIntensity = pl.radiance * pl.area;
        pl.sourceRadius = pl.bDisk ? halfWidth : 0.5 * (halfWidth + halfHeight);
        pl.oneSided = true;
        pl.cosConeInner = light.normal.w;
        pl.cosConeOuter = light.position.w;
        pl.rightAxis = normalize(mul((float3x3)view, light.right.xyz));
        pl.upAxis = normalize(mul((float3x3)view, light.up.xyz));
        pl.halfWidth = halfWidth;
        pl.halfHeight = halfHeight;
    }
    else
    {
        // LIGHT_TYPE_TRIANGLE not supported by the punctual path.
        pl.valid = false;
    }
    return pl;
}

/** Direction to the point on the light nearest the specular reflection ray, so large sources give
 *  broad highlights (Karis 2013). A rect ray that misses its plane clamps a far point along the ray instead of
 *  snapping to the center: both branches meet at the rect edge as the ray goes parallel, so the highlight stays continuous. */
float3 RepresentativePointL(PunctualLight pl, float3 N, float3 V, float3 viewPos, float distToCenter)
{
    float3 R = reflect(-V, N);
    float3 toCenter = pl.pos - viewPos;

    if (pl.oneSided)
    {
        float denom = dot(R, pl.dir);
        float t = dot(toCenter, pl.dir) / denom;
        bool bHitsPlane = abs(denom) >= 1e-4 && t > 0.0;
        float3 rel = (viewPos + R * (bHitsPlane ? t : 1e4)) - pl.pos;
        float u = dot(rel, pl.rightAxis);
        float v = dot(rel, pl.upAxis);
        if (pl.bDisk)
        {
            float2 uv = float2(u, v);
            uv *= saturate(pl.halfWidth / max(length(uv), 1e-5));
            u = uv.x;
            v = uv.y;
        }
        else
        {
            u = clamp(u, -pl.halfWidth, pl.halfWidth);
            v = clamp(v, -pl.halfHeight, pl.halfHeight);
        }
        return normalize((pl.pos + u * pl.rightAxis + v * pl.upAxis) - viewPos);
    }

    float3 centerToRay = dot(toCenter, R) * R - toCenter;
    float3 closestVec = toCenter + centerToRay * saturate(pl.sourceRadius / max(length(centerToRay), 1e-5));
    return normalize(closestVec);
}

/** Direction, distance and irradiance scale toward the light. bBeam: the cone footprint is smaller than the emitter, so it lights along its normal like a bounded directional light. */
bool PunctualIncidence(PunctualLight pl, float3 viewPos, out float3 L, out float dist, out float irradiance, out bool bBeam)
{
    L = float3(0.0);
    dist = 0.0;
    irradiance = 0.0;
    bBeam = false;

    ConeClip clip;
    if (pl.oneSided && LightConeClip(pl.pos, pl.dir, pl.rightAxis, pl.upAxis, pl.halfWidth, pl.halfHeight, pl.bDisk, pl.cosConeOuter, pl.area, viewPos, clip))
    {
        bBeam = true;
        float coverage = LightConeClipCoverage(clip);
        if (coverage <= 0.0) { return false; }
        dist = dot(viewPos - pl.pos, pl.dir);
        if (dist >= pl.range) { return false; }
        L = -pl.dir;
        irradiance = coverage * PI * LightConeIntegral(pl.cosConeInner, pl.cosConeOuter) * pl.radiance * LightRangeCull(dist, pl.range) * LightFalloffScale(dist, pl.falloffBias);
        return true;
    }

    float3 toLight = pl.pos - viewPos;
    dist = length(toLight);
    if (dist >= pl.range) { return false; }
    L = toLight / max(dist, 1e-5);

    float emitterCos = 1.0;
    if (pl.oneSided)
    {
        emitterCos = max(dot(pl.dir, -L), 0.0);
        if (emitterCos <= 0.0) { return false; }
        emitterCos *= LightConeFalloff(emitterCos, pl.cosConeInner, pl.cosConeOuter);
    }
    irradiance = LightDistanceFalloff(dist, pl.range) * LightFalloffScale(dist, pl.falloffBias) * emitterCos * pl.effectiveIntensity;
    return true;
}

/** A beam gives every point the same L, so V == -L is routine (radiance cache cells shade with V = N). */
float3 SafeHalfVector(float3 V, float3 L, float3 N)
{
    float3 H = V + L;
    float lenSq = dot(H, H);
    return lenSq > 1e-12 ? H * rsqrt(lenSq) : N;
}

/** Shade a degenerated punctual light: Oren-Nayar diffuse + representative-point GGX specular. */
public float3 ShadePunctual(PunctualLight pl, float3 N, float3 V, float3 viewPos, float3 albedo, float metallic, float roughness, float3 F0)
{
    if (!pl.valid) { return float3(0.0); }

    float3 L;
    float dist;
    float irradiance;
    bool bBeam;
    if (!PunctualIncidence(pl, viewPos, L, dist, irradiance, bBeam)) { return float3(0.0); }

    float nDotV = max(dot(N, V), 0.0);
    float nDotL = max(dot(N, L), 0.0);

    float3 kD = (1.0 - F_SCHLICK(V, SafeHalfVector(V, L, N), F0)) * (1.0 - metallic);
    float orenNayar = Diffuse_OrenNayar(nDotL, nDotV, L, V, roughness);
    float3 diffuse = kD * albedo * orenNayar * nDotL;

    // Representative-point specular; widen roughness by the source's angular size and renormalize.
    float3 Ls = L;
    float specRoughness = roughness;
    float specNormalization = 1.0;
    if (pl.sourceRadius > 0.0 && !bBeam)
    {
        Ls = RepresentativePointL(pl, N, V, viewPos, dist);
        float alpha = roughness * roughness;
        float alphaPrime = saturate(alpha + pl.sourceRadius / (2.0 * max(dist, 1e-4)));
        specRoughness = sqrt(alphaPrime);
        specNormalization = alpha / max(alphaPrime, 1e-6);
        specNormalization *= specNormalization;
    }

    float3 Hs = SafeHalfVector(V, Ls, N);
    float nDotLs = max(dot(N, Ls), 0.0);
    float NDF = D_GGX(N, Hs, specRoughness);
    float G = G_SMITH_HEIGHT_CORRELATED(N, V, Ls, specRoughness);
    float3 F = F_SCHLICK(V, Hs, F0);
    float3 specular = (NDF * G * F) / max(4.0 * nDotV * nDotLs, 0.001) * nDotLs * specNormalization;

    return (diffuse + specular) * irradiance * pl.color;
}

/** Diffuse-only punctual shade for view-independent sinks (radiance cache): identical to ShadePunctual minus the representative-point specular lobe, which is meaningless when V is forced to N. */
public float3 ShadePunctualDiffuse(PunctualLight pl, float3 N, float3 V, float3 viewPos, float3 albedo, float metallic, float roughness, float3 F0)
{
    if (!pl.valid) { return float3(0.0); }

    float3 L;
    float dist;
    float irradiance;
    bool bBeam;
    if (!PunctualIncidence(pl, viewPos, L, dist, irradiance, bBeam)) { return float3(0.0); }

    float nDotV = max(dot(N, V), 0.0);
    float nDotL = max(dot(N, L), 0.0);

    float3 kD = (1.0 - F_SCHLICK(V, SafeHalfVector(V, L, N), F0)) * (1.0 - metallic);
    float orenNayar = Diffuse_OrenNayar(nDotL, nDotV, L, V, roughness);
    float3 diffuse = kD * albedo * orenNayar * nDotL;

    return diffuse * irradiance * pl.color;
}
