import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;

static const float  SMAA_THRESHOLD                      = 0.1;
static const float  SMAA_LOCAL_CONTRAST_ADAPTATION      = 2.0;
static const int    SMAA_MAX_SEARCH_STEPS               = 32;
static const float  SMAA_AREATEX_MAX_DISTANCE           = 16.0;
static const float2 SMAA_AREATEX_PIXEL_SIZE             = float2(1.0 / 160.0, 1.0 / 560.0);
static const float  SMAA_AREATEX_SUBTEX_SIZE            = 1.0 / 7.0;
static const float  SMAA_CORNER_ROUNDING                = 0.25;

// ---- Helpers ----
float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Searches along a row for the farthest edge in one direction.
// Returns the UV of the stopping point.
float2 SearchXLeft(uint edgeIndex, float2 uv, float2 texelSize, float end)
{
    uv -= float2(1.5, 0.0) * texelSize;
    float e = 0.0;
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        e = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).g;
        if (e < 0.9 || uv.x <= end) { break; }
        uv.x -= 2.0 * texelSize.x;
    }
    return uv;
}

float2 SearchXRight(uint edgeIndex, float2 uv, float2 texelSize, float end)
{
    uv += float2(1.5, 0.0) * texelSize;
    float e = 0.0;
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        e = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).g;
        if (e < 0.9 || uv.x >= end) { break; }
        uv.x += 2.0 * texelSize.x;
    }
    return uv;
}

float2 SearchYUp(uint edgeIndex, float2 uv, float2 texelSize, float end)
{
    uv -= float2(0.0, 1.5) * texelSize;
    float e = 0.0;
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        e = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
        if (e < 0.9 || uv.y <= end) { break; }
        uv.y -= 2.0 * texelSize.y;
    }
    return uv;
}

float2 SearchYDown(uint edgeIndex, float2 uv, float2 texelSize, float end)
{
    uv += float2(0.0, 1.5) * texelSize;
    float e = 0.0;
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        e = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
        if (e < 0.9 || uv.y >= end) { break; }
        uv.y += 2.0 * texelSize.y;
    }
    return uv;
}

// Maps crossing distances + pattern index to area texture UV.
// e: float2 encoding which crossing edges are present (0 or 1 each component)
float2 AreaTexCoord(float2 dist, float2 e, float offset)
{
    float2 texcoord = SMAA_AREATEX_MAX_DISTANCE * round(4.0 * e) + dist;
    texcoord = SMAA_AREATEX_PIXEL_SIZE * (texcoord + 0.5) + float2(0.0, SMAA_AREATEX_SUBTEX_SIZE * offset);
    return texcoord;
}

// Corner rounding: reduces weights near sharp corners to preserve them.
void DetectHorizontalCornerPattern(uint edgeIndex, inout float2 weights, float2 uv, float2 texelSize, float2 d)
{
    float2 leftRight = step(d.xy, d.yx);
    float2 rounding = SMAA_CORNER_ROUNDING * leftRight;

    float2 factor = float2(1.0, 1.0);
    factor.x -= rounding.x * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(0.0,  texelSize.y), 0).r;
    factor.x -= rounding.y * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(d.x,  texelSize.y) * texelSize, 0).r;
    factor.y -= rounding.x * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(0.0, -texelSize.y), 0).r;
    factor.y -= rounding.y * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(d.x, -texelSize.y) * texelSize, 0).r;

    weights *= saturate(factor);
}

void DetectVerticalCornerPattern(uint edgeIndex, inout float2 weights, float2 uv, float2 texelSize, float2 d)
{
    float2 leftRight = step(d.xy, d.yx);
    float2 rounding = SMAA_CORNER_ROUNDING * leftRight;

    float2 factor = float2(1.0, 1.0);
    factor.x -= rounding.x * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( texelSize.x, 0.0), 0).g;
    factor.x -= rounding.y * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( texelSize.x, d.x) * texelSize, 0).g;
    factor.y -= rounding.x * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-texelSize.x, 0.0), 0).g;
    factor.y -= rounding.y * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-texelSize.x, d.x) * texelSize, 0).g;

    weights *= saturate(factor);
}

// ============================================================
// Pass 1: Edge Detection
// ============================================================
[shader("compute")]
[numthreads(16, 16, 1)]
void EdgeDetectionMain(uniform SmaaEdgeDetectionPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2 coord    = int2(threadID.xy);
    float2 uv     = (float2(coord) + 0.5) * pc.sceneData.texelSize;
    float2 tsize  = pc.sceneData.texelSize;

    float3 cc = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord,             0)).rgb;
    float3 cl = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(-1,0), 0)).rgb;
    float3 ct = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(0,-1), 0)).rgb;

    float L    = Luma(cc);
    float Ll   = Luma(cl);
    float Lt   = Luma(ct);

    float2 delta = abs(float2(L - Ll, L - Lt));
    float2 edges = step(SMAA_THRESHOLD, delta);

    // Local contrast adaptation: suppress edges that are weaker than their surroundings
    float3 cr = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(1, 0), 0)).rgb;
    float3 cb = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(0, 1), 0)).rgb;
    float Lr   = Luma(cr);
    float Lb   = Luma(cb);

    float2 maxDelta = max(delta, abs(float2(L - Lr, L - Lb)));

    float3 cll = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(-2, 0), 0)).rgb;
    float3 ctt = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(0, -2), 0)).rgb;
    maxDelta = max(maxDelta, abs(float2(Ll - Luma(cll), Lt - Luma(ctt))));

    float finalDelta = max(maxDelta.x, maxDelta.y);
    edges *= step(finalDelta * (1.0 / SMAA_LOCAL_CONTRAST_ADAPTATION), delta);

    rdgStorageFloat2[pc.outputEdgeIndex][coord] = edges;
}

// ============================================================
// Pass 2: Blend Weight Calculation
// ============================================================
[shader("compute")]
[numthreads(16, 16, 1)]
void BlendWeightMain(uniform SmaaBlendWeightPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2  coord   = int2(threadID.xy);
    float2 uv     = (float2(coord) + 0.5) * pc.sceneData.texelSize;
    float2 tsize  = pc.sceneData.texelSize;

    float4 weights = float4(0.0);

    float2 e = rdgTexturesFloat2[pc.edgeIndex].Load(int3(coord, 0));

    // ---- Horizontal edge (R channel = top edge at this pixel) ----
    if (e.g > 0.0) {
        // Search left and right along the top edge
        float2 d;
        float2 coords;

        // Left
        coords = SearchXLeft(pc.edgeIndex, uv, tsize, uv.x - float(SMAA_MAX_SEARCH_STEPS) * tsize.x);
        d.x = coords.x;
        float e1 = rdgTexturesFloat2[pc.edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], coords + float2(-0.5, 0.0) * tsize, 0).r;

        // Right
        coords = SearchXRight(pc.edgeIndex, uv, tsize, uv.x + float(SMAA_MAX_SEARCH_STEPS) * tsize.x);
        d.y = coords.x;
        float e2 = rdgTexturesFloat2[pc.edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], coords + float2(0.5, 0.0) * tsize, 0).r;

        // Convert to pixel distances
        d = abs(round(d / tsize.x - uv.x / tsize.x));

        // Area texture lookup
        float2 sqrtD = sqrt(d);
        float2 areaUV = AreaTexCoord(sqrtD, float2(e1, e2), 0.0);
        weights.rg = rdgTexturesFloat2[pc.areaTexIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], areaUV, 0);

        DetectHorizontalCornerPattern(pc.edgeIndex, weights.rg, uv, tsize, d);
    }

    // ---- Vertical edge (G channel = left edge at this pixel) ----
    if (e.r > 0.0) {
        float2 d;
        float2 coords;

        // Up
        coords = SearchYUp(pc.edgeIndex, uv, tsize, uv.y - float(SMAA_MAX_SEARCH_STEPS) * tsize.y);
        d.x = coords.y;
        float e1 = rdgTexturesFloat2[pc.edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], coords + float2(0.0, -0.5) * tsize, 0).g;

        // Down
        coords = SearchYDown(pc.edgeIndex, uv, tsize, uv.y + float(SMAA_MAX_SEARCH_STEPS) * tsize.y);
        d.y = coords.y;
        float e2 = rdgTexturesFloat2[pc.edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], coords + float2(0.0, 0.5) * tsize, 0).g;

        d = abs(round(d / tsize.y - uv.y / tsize.y));

        float2 sqrtD = sqrt(d);
        float2 areaUV = AreaTexCoord(sqrtD, float2(e1, e2), 0.0);
        weights.ba = rdgTexturesFloat2[pc.areaTexIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], areaUV, 0);

        DetectVerticalCornerPattern(pc.edgeIndex, weights.ba, uv, tsize, d);
    }

    rdgStorageFloat4[pc.outputBlendIndex][coord] = weights;
}

// ============================================================
// Pass 3: Neighborhood Blending
// ============================================================
[shader("compute")]
[numthreads(16, 16, 1)]
void NeighborhoodBlendMain(uniform SmaaNeighborhoodBlendPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2  coord  = int2(threadID.xy);
    float2 uv    = (float2(coord) + 0.5) * pc.sceneData.texelSize;
    float2 tsize = pc.sceneData.texelSize;

    float4 a;
    a.x = rdgTexturesFloat4[pc.blendWeightIndex].Load(int3(coord + int2( 0,  1), 0)).a; // top    .ba of pixel above
    a.y = rdgTexturesFloat4[pc.blendWeightIndex].Load(int3(coord,               0)).b; // bottom .b  of this pixel
    a.z = rdgTexturesFloat4[pc.blendWeightIndex].Load(int3(coord + int2( 1,  0), 0)).g; // right  .g  of right pixel
    a.w = rdgTexturesFloat4[pc.blendWeightIndex].Load(int3(coord,               0)).r; // left   .r  of this pixel

    float3 result;

    if (dot(a, float4(1.0)) < 1e-5) {
        // No blending needed
        result = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord, 0)).rgb;
    } else {
        bool h = max(a.x, a.y) > max(a.z, a.w);

        float4 blendingOffset = float4(0.0, a.x, 0.0, a.y);
        float2 blendingWeight = a.xy;
        if (h) {
            blendingOffset = float4(a.z, 0.0, a.w, 0.0);
            blendingWeight = a.zw;
        }
        blendingWeight /= dot(blendingWeight, float2(1.0, 1.0));

        float4 blendingCoord = float4(uv, uv) + (h
            ? float4( blendingOffset.z, 0.0, -blendingOffset.w, 0.0)
            : float4(0.0,  blendingOffset.x, 0.0, -blendingOffset.y)) * float4(tsize, tsize);

        float3 color0 = rdgTexturesFloat4[pc.colorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], blendingCoord.xy, 0).rgb;
        float3 color1 = rdgTexturesFloat4[pc.colorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], blendingCoord.zw, 0).rgb;

        result = blendingWeight.x * color0 + blendingWeight.y * color1;
    }

    rdgStorageFloat4[pc.outputIndex][coord] = float4(result, 1.0);
}
