import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;

// https://github.com/iryoku/smaa/blob/master/SMAA.hlsl

static const float SMAA_DEPTH_THRESHOLD = 0.1;

// ============================================================
// Pass 1a: Luma Edge Detection
//
// Requires gamma-corrected (linear) color input.
// Standard choice: good quality, low cost.
// ============================================================
[shader("compute")]
[numthreads(16, 16, 1)]
void LumaEdgeDetectionMain(uniform SmaaEdgeDetectionPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2 coord   = int2(threadID.xy);
    float2 tsize = pc.sceneData.texelSize;

    float3 C   = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord,                0)).rgb;
    float3 Cl  = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(-1,  0), 0)).rgb;
    float3 Ct  = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 0, -1), 0)).rgb;
    float3 Cr  = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 1,  0), 0)).rgb;
    float3 Cb  = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 0,  1), 0)).rgb;
    float3 Cll = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(-2,  0), 0)).rgb;
    float3 Ctt = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 0, -2), 0)).rgb;

    float L   = Luma(C);
    float Ll  = Luma(Cl);
    float Lt  = Luma(Ct);

    float4 delta;
    delta.xy = abs(L - float2(Ll, Lt));
    float2 edges = step(pc.threshold, delta.xy);

    delta.zw   = abs(L - float2(Luma(Cr), Luma(Cb)));
    float2 maxDelta = max(delta.xy, delta.zw);
    maxDelta = max(maxDelta, abs(float2(Ll - Luma(Cll), Lt - Luma(Ctt))));

    edges *= step(max(maxDelta.x, maxDelta.y), pc.localContrastAdaptation * delta.xy);

    rdgStorageFloat2[pc.outputEdgeIndex][coord] = edges;
}

// ============================================================
// Pass 1b: Color Edge Detection
//
// Catches edges luma misses (e.g. isoluminant color boundaries).
// Higher quality than luma, ~3x more samples.
// ============================================================
[shader("compute")]
[numthreads(16, 16, 1)]
void ColorEdgeDetectionMain(uniform SmaaEdgeDetectionPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2 coord   = int2(threadID.xy);
    float2 tsize = pc.sceneData.texelSize;

    float3 C  = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord,                0)).rgb;
    float3 Cl = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(-1,  0), 0)).rgb;
    float3 Ct = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 0, -1), 0)).rgb;

    float4 delta;
    float3 t;

    t = abs(C - Cl); delta.x = max(max(t.r, t.g), t.b);
    t = abs(C - Ct); delta.y = max(max(t.r, t.g), t.b);

    float2 edges = step(pc.threshold, delta.xy);

    float3 Cr = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 1,  0), 0)).rgb;
    float3 Cb = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 0,  1), 0)).rgb;

    t = abs(C - Cr); delta.z = max(max(t.r, t.g), t.b);
    t = abs(C - Cb); delta.w = max(max(t.r, t.g), t.b);

    float2 maxDelta = max(delta.xy, delta.zw);

    float3 Cll = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(-2,  0), 0)).rgb;
    float3 Ctt = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 0, -2), 0)).rgb;

    t = abs(Cl - Cll); delta.z = max(max(t.r, t.g), t.b);
    t = abs(Ct - Ctt); delta.w = max(max(t.r, t.g), t.b);

    maxDelta = max(maxDelta, delta.zw);

    edges *= step(max(maxDelta.x, maxDelta.y), pc.localContrastAdaptation * delta.xy);

    rdgStorageFloat2[pc.outputEdgeIndex][coord] = edges;
}

// ============================================================
// Pass 1c: Depth Edge Detection
//
// Geometry edges only. Cheapest option, misses texture/color edges.
// ============================================================
[shader("compute")]
[numthreads(16, 16, 1)]
void DepthEdgeDetectionMain(uniform SmaaEdgeDetectionPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2 coord = int2(threadID.xy);

    float D   = rdgTexturesFloat[pc.depthIndex].Load(int3(coord,                0));
    float Dl  = rdgTexturesFloat[pc.depthIndex].Load(int3(coord + int2(-1,  0), 0));
    float Dt  = rdgTexturesFloat[pc.depthIndex].Load(int3(coord + int2( 0, -1), 0));

    float2 delta = abs(D - float2(Dl, Dt));
    float2 edges = step(SMAA_DEPTH_THRESHOLD, delta);

    rdgStorageFloat2[pc.outputEdgeIndex][coord] = edges;
}

static const int    SMAA_MAX_SEARCH_STEPS          = 32;
static const int    SMAA_MAX_SEARCH_STEPS_DIAG     = 8;
static const float  SMAA_AREATEX_MAX_DISTANCE      = 16.0;
static const float  SMAA_AREATEX_MAX_DISTANCE_DIAG = 20.0;
static const float2 SMAA_AREATEX_PIXEL_SIZE        = float2(1.0 / 160.0, 1.0 / 560.0);
static const float  SMAA_AREATEX_SUBTEX_SIZE       = 1.0 / 7.0;
static const float  SMAA_CORNER_ROUNDING           = 0.25; // normalized [0,1], matches reference default of 25/100
static const float2 SMAA_SEARCHTEX_SIZE            = float2(66.0, 33.0);
static const float2 SMAA_SEARCHTEX_PACKED_SIZE     = float2(64.0, 16.0);

//-----------------------------------------------------------------------------
// Diagonal Search Functions

float2 SMAADecodeDiagBilinearAccess(float2 e)
{
    e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);
    return round(e);
}

float4 SMAADecodeDiagBilinearAccess(float4 e)
{
    e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);
    return round(e);
}

float2 SMAASearchDiag1(uint edgeIndex, float2 texcoord, float2 dir, out float2 e, float2 tsize, int maxStepsDiag)
{
    float4 coord = float4(texcoord, -1.0, 1.0);
    while (coord.z < float(maxStepsDiag - 1) && coord.w > 0.9) {
        coord.xyz = mad(float3(dir, 1.0), float3(tsize, 1.0), coord.xyz);
        e = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], coord.xy, 0).rg;
        coord.w = dot(e, float2(0.5, 0.5));
    }
    return coord.zw;
}

float2 SMAASearchDiag2(uint edgeIndex, float2 texcoord, float2 dir, out float2 e, float2 tsize, int maxStepsDiag)
{
    float4 coord = float4(texcoord, -1.0, 1.0);
    coord.x += 0.25 * tsize.x;
    while (coord.z < float(maxStepsDiag - 1) && coord.w > 0.9) {
        coord.xyz = mad(float3(dir, 1.0), float3(tsize, 1.0), coord.xyz);
        e = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], coord.xy, 0).rg;
        e = SMAADecodeDiagBilinearAccess(e);
        coord.w = dot(e, float2(0.5, 0.5));
    }
    return coord.zw;
}

float2 SMAAAreaDiag(float2 dist, float2 e, float2 tsize)
{
    float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE_DIAG, SMAA_AREATEX_MAX_DISTANCE_DIAG), e, dist);
    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);
    texcoord.x += 0.5; // diagonal areas are on the right half of the area texture
    return assetTextures[SMAA_AREA_BINDLESS_INDEX].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], texcoord, 0).rg;
}

float2 SMAACalculateDiagWeights(uint edgeIndex, float2 texcoord, float2 e, float2 tsize, int maxStepsDiag)
{
    float2 weights = float2(0.0, 0.0);
    float4 d;
    float2 end;

    if (e.r > 0.0) {
        d.xz = SMAASearchDiag1(edgeIndex, texcoord, float2(-1.0,  1.0), end, tsize, maxStepsDiag);
        d.x += float(end.y > 0.9);
    } else {
        d.xz = float2(0.0, 0.0);
    }
    d.yw = SMAASearchDiag1(edgeIndex, texcoord, float2(1.0, -1.0), end, tsize, maxStepsDiag);

    if (d.x + d.y > 2.0) {
        float4 coords = mad(float4(-d.x + 0.25, d.x, d.y, -d.y - 0.25), tsize.xyxy, texcoord.xyxy);
        float4 c;
        c.xy = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], coords.xy + float2(-1, 0) * tsize, 0).rg;
        c.zw = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], coords.zw + float2( 1, 0) * tsize, 0).rg;
        c.yxwz = SMAADecodeDiagBilinearAccess(c.xyzw);

        float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw);
        if (step(0.9, d.z) > 0.0) { cc.x = 0.0; }
        if (step(0.9, d.w) > 0.0) { cc.y = 0.0; }

        weights += SMAAAreaDiag(d.xy, cc, tsize);
    }

    d.xz = SMAASearchDiag2(edgeIndex, texcoord, float2(-1.0, -1.0), end, tsize, maxStepsDiag);
    if (rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], texcoord + float2(1, 0) * tsize, 0).r > 0.0) {
        d.yw = SMAASearchDiag2(edgeIndex, texcoord, float2(1.0, 1.0), end, tsize, maxStepsDiag);
        d.y += float(end.y > 0.9);
    } else {
        d.yw = float2(0.0, 0.0);
    }

    if (d.x + d.y > 2.0) {
        float4 coords = mad(float4(-d.x, -d.x, d.y, d.y), tsize.xyxy, texcoord.xyxy);
        float4 c;
        c.x  = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], coords.xy + float2(-1,  0) * tsize, 0).g;
        c.y  = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], coords.xy + float2( 0, -1) * tsize, 0).r;
        c.zw = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], coords.zw + float2( 1,  0) * tsize, 0).gr;

        float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw);
        if (step(0.9, d.z) > 0.0) { cc.x = 0.0; }
        if (step(0.9, d.w) > 0.0) { cc.y = 0.0; }

        weights += SMAAAreaDiag(d.xy, cc, tsize).gr;
    }

    return weights;
}

//-----------------------------------------------------------------------------
// Horizontal/Vertical Search Functions

float SMAASearchLength(float2 e, float offset)
{
    float2 scale = SMAA_SEARCHTEX_SIZE * float2(0.5, -1.0);
    float2 bias  = SMAA_SEARCHTEX_SIZE * float2(offset, 1.0);
    scale += float2(-1.0,  1.0);
    bias  += float2( 0.5, -0.5);
    scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
    bias  *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
    return assetTextures[SMAA_SEARCH_BINDLESS_INDEX].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], mad(scale, e, bias), 0).r;
}

// texcoord should be pre-biased by (-0.25, -0.125) * tsize before calling (@PSEUDO_GATHER4)
float SMAASearchXLeft(uint edgeIndex, float2 texcoord, float end, float2 tsize)
{
    float2 e = float2(0.0, 1.0);
    while (texcoord.x > end && e.g > 0.8281 && e.r == 0.0) {
        e = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], texcoord, 0).rg;
        texcoord = mad(-float2(2.0, 0.0), tsize, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(e, 0.0), 3.25);
    return mad(tsize.x, offset, texcoord.x);
}

// texcoord should be pre-biased by (1.25, -0.125) * tsize before calling
float SMAASearchXRight(uint edgeIndex, float2 texcoord, float end, float2 tsize)
{
    float2 e = float2(0.0, 1.0);
    while (texcoord.x < end && e.g > 0.8281 && e.r == 0.0) {
        e = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], texcoord, 0).rg;
        texcoord = mad(float2(2.0, 0.0), tsize, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(e, 0.5), 3.25);
    return mad(-tsize.x, offset, texcoord.x);
}

// texcoord should be pre-biased by (-0.125, -0.25) * tsize before calling
float SMAASearchYUp(uint edgeIndex, float2 texcoord, float end, float2 tsize)
{
    float2 e = float2(1.0, 0.0);
    while (texcoord.y > end && e.r > 0.8281 && e.g == 0.0) {
        e = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], texcoord, 0).rg;
        texcoord = mad(-float2(0.0, 2.0), tsize, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(e.gr, 0.0), 3.25);
    return mad(tsize.y, offset, texcoord.y);
}

// texcoord should be pre-biased by (-0.125, 1.25) * tsize before calling
float SMAASearchYDown(uint edgeIndex, float2 texcoord, float end, float2 tsize)
{
    float2 e = float2(1.0, 0.0);
    while (texcoord.y < end && e.r > 0.8281 && e.g == 0.0) {
        e = rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], texcoord, 0).rg;
        texcoord = mad(float2(0.0, 2.0), tsize, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(e.gr, 0.5), 3.25);
    return mad(-tsize.y, offset, texcoord.y);
}

float2 SMAAArea(float2 dist, float e1, float e2)
{
    float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE, SMAA_AREATEX_MAX_DISTANCE), round(4.0 * float2(e1, e2)), dist);
    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);
    return assetTextures[SMAA_AREA_BINDLESS_INDEX].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], texcoord, 0).rg;
}

//-----------------------------------------------------------------------------
// Corner Detection Functions

// leftUV  = float2(left_end_x,   uv.y)
// rightUV = float2(right_end_x,  uv.y)
void SMAADetectHorizontalCornerPattern(uint edgeIndex, inout float2 weights, float2 leftUV, float2 rightUV, float2 d, float2 tsize)
{
    float2 leftRight = step(d.xy, d.yx);
    float2 rounding  = (1.0 - SMAA_CORNER_ROUNDING) * leftRight;
    rounding /= leftRight.x + leftRight.y;

    float2 factor = float2(1.0, 1.0);
    factor.x -= rounding.x * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], leftUV  + float2( 0,  1) * tsize, 0).r;
    factor.x -= rounding.y * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], rightUV + float2( 1,  1) * tsize, 0).r;
    factor.y -= rounding.x * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], leftUV  + float2( 0, -2) * tsize, 0).r;
    factor.y -= rounding.y * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], rightUV + float2( 1, -2) * tsize, 0).r;

    weights *= saturate(factor);
}

// topUV    = float2(uv.x, top_end_y)
// bottomUV = float2(uv.x, bottom_end_y)
void SMAADetectVerticalCornerPattern(uint edgeIndex, inout float2 weights, float2 topUV, float2 bottomUV, float2 d, float2 tsize)
{
    float2 leftRight = step(d.xy, d.yx);
    float2 rounding  = (1.0 - SMAA_CORNER_ROUNDING) * leftRight;
    rounding /= leftRight.x + leftRight.y;

    float2 factor = float2(1.0, 1.0);
    factor.x -= rounding.x * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], topUV    + float2( 1,  0) * tsize, 0).g;
    factor.x -= rounding.y * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], bottomUV + float2( 1,  1) * tsize, 0).g;
    factor.y -= rounding.x * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], topUV    + float2(-2,  0) * tsize, 0).g;
    factor.y -= rounding.y * rdgTexturesFloat2[edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], bottomUV + float2(-2,  1) * tsize, 0).g;

    weights *= saturate(factor);
}

// ============================================================
// Pass 2: Blend Weight Calculation
// ============================================================
[shader("compute")]
[numthreads(16, 16, 1)]
void BlendWeightMain(uniform SmaaBlendWeightPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2   coord  = int2(threadID.xy);
    float2 uv     = (float2(coord) + 0.5) * pc.sceneData.texelSize;
    float2 tsize  = pc.sceneData.texelSize;

    float4 weights = float4(0.0);
    float2 e = rdgTexturesFloat2[pc.edgeIndex].Load(int3(coord, 0));

    // ---- Horizontal edge (g = top edge present at this pixel) ----
    if (e.g > 0.0) {
        // Diagonal detection: runs first and takes priority.
        weights.rg = SMAACalculateDiagWeights(pc.edgeIndex, uv, e, tsize, pc.maxSearchStepsDiag);

        if (weights.r == -weights.g) { // no diagonal found (r + g == 0)
            float2 d;

            // Pre-bias search start coords by (-0.25, -0.125) and (1.25, -0.125) (@PSEUDO_GATHER4).
            // End limits account for 2-texel steps over SMAA_MAX_SEARCH_STEPS iterations.
            float2 coordsL = uv + float2(-0.25, -0.125) * tsize;
            float2 coordsR = uv + float2( 1.25, -0.125) * tsize;
            float  endL    = coordsL.x - 2.0 * float(pc.maxSearchSteps) * tsize.x;
            float  endR    = coordsR.x + 2.0 * float(pc.maxSearchSteps) * tsize.x;

            d.x = SMAASearchXLeft(pc.edgeIndex, coordsL, endL, tsize);
            d.y = SMAASearchXRight(pc.edgeIndex, coordsR, endR, tsize);

            // Crossing edge samples: y offset of -0.25 disambiguates which row the edge is on.
            float crossingY = uv.y - 0.25 * tsize.y;
            float e1 = rdgTexturesFloat2[pc.edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], float2(d.x, crossingY), 0).r;
            float e2 = rdgTexturesFloat2[pc.edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], float2(d.y, crossingY) + float2(tsize.x, 0.0), 0).r;

            // Convert from UV x coords to pixel distances.
            float2 pixDist = abs(round(d / tsize.x - uv.x / tsize.x));

            weights.rg = SMAAArea(sqrt(pixDist), e1, e2);
            SMAADetectHorizontalCornerPattern(pc.edgeIndex, weights.rg, float2(d.x, uv.y), float2(d.y, uv.y), pixDist, tsize);
        } else {
            e.r = 0.0; // diagonal found; skip vertical processing
        }
    }

    // ---- Vertical edge (r = left edge present at this pixel) ----
    if (e.r > 0.0) {
        float2 d;

        float2 coordsU = uv + float2(-0.125, -0.25) * tsize;
        float2 coordsD = uv + float2(-0.125,  1.25) * tsize;
        float  endU    = coordsU.y - 2.0 * float(pc.maxSearchSteps) * tsize.y;
        float  endD    = coordsD.y + 2.0 * float(pc.maxSearchSteps) * tsize.y;

        d.x = SMAASearchYUp(pc.edgeIndex, coordsU, endU, tsize);
        d.y = SMAASearchYDown(pc.edgeIndex, coordsD, endD, tsize);

        // Crossing edge samples: x offset of -0.25 disambiguates which column the edge is on.
        float crossingX = uv.x - 0.25 * tsize.x;
        float e1 = rdgTexturesFloat2[pc.edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], float2(crossingX, d.x), 0).g;
        float e2 = rdgTexturesFloat2[pc.edgeIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], float2(crossingX, d.y) + float2(0.0, tsize.y), 0).g;

        float2 pixDist = abs(round(d / tsize.y - uv.y / tsize.y));

        weights.ba = SMAAArea(sqrt(pixDist), e1, e2);
        SMAADetectVerticalCornerPattern(pc.edgeIndex, weights.ba, float2(uv.x, d.x), float2(uv.x, d.y), pixDist, tsize);
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

    int2   coord  = int2(threadID.xy);
    float2 uv     = (float2(coord) + 0.5) * pc.sceneData.texelSize;
    float2 tsize  = pc.sceneData.texelSize;

    // Gather blend weights from the neighborhood.
    // Weight packing from pass 2: rg = horizontal edge weights, ba = vertical edge weights.
    // To blend rightward: read the right pixel's .a (its vertical-left weight pointing back at us).
    // To blend downward:  read the below pixel's .g (its horizontal-top weight pointing back at us).
    float4 a;
    a.x = rdgTexturesFloat4[pc.blendWeightIndex].Load(int3(coord + int2(1, 0), 0)).a; // right pixel
    a.y = rdgTexturesFloat4[pc.blendWeightIndex].Load(int3(coord + int2(0, 1), 0)).g; // below pixel
    a.z = rdgTexturesFloat4[pc.blendWeightIndex].Load(int3(coord,              0)).b; // current
    a.w = rdgTexturesFloat4[pc.blendWeightIndex].Load(int3(coord,              0)).r; // current

    float3 result = float3(0.0f);

    if (dot(a, float4(1.0)) < 1e-5) {
        result = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord, 0)).rgb;
    } else {
        // Horizontal dominates when right/left weights exceed top/bottom weights.
        bool h = max(a.x, a.z) > max(a.y, a.w);

        float4 blendingOffset = h ? float4(a.x, 0.0, a.z, 0.0) : float4(0.0, a.y, 0.0, a.w);
        float2 blendingWeight = h ? a.xz : a.yw;
        blendingWeight /= dot(blendingWeight, float2(1.0, 1.0));

        // Positive offset blends toward right/below, negative toward left/above.
        float4 blendingCoord = uv.xyxy + blendingOffset * float4(tsize.x, tsize.y, -tsize.x, -tsize.y);

        float3 color0 = rdgTexturesFloat4[pc.colorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], blendingCoord.xy, 0).rgb;
        float3 color1 = rdgTexturesFloat4[pc.colorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], blendingCoord.zw, 0).rgb;

        result = blendingWeight.x * color0 + blendingWeight.y * color1;
    }

    rdgStorageFloat4[pc.outputIndex][coord] = float4(result, 1.0);
}

// ============================================================
// Pass 4: Temporal Resolve (SMAA T2x)
// ============================================================
static const float SMAA_REPROJECTION_WEIGHT_SCALE = 30.0;

[shader("compute")]
[numthreads(16, 16, 1)]
void TemporalResolveMain(uniform SmaaTemporalResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2   coord = int2(threadID.xy);
    float2 uv    = (float2(coord) + 0.5) * pc.sceneData.texelSize;

    uint4  g1          = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(coord, 0));
    float2 velocityNDC = UnpackMotionVectors(g1.g);
    float2 velocity    = velocityNDC * 0.5;

    float4 current  = rdgTexturesFloat4[pc.currentColorIndex].Load(int3(coord, 0));
    float4 previous = rdgTexturesFloat4[pc.previousColorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + velocity, 0);

    float delta  = length(velocity);
    float weight = 0.5 * saturate(1.0 - delta * SMAA_REPROJECTION_WEIGHT_SCALE);

    rdgStorageFloat4[pc.outputIndex][coord] = lerp(current, previous, weight);
}
