import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;

import shader_constants;
import shader_functions;
import bindless_declarations;

// ==================== Barycentric Derivatives ====================
struct BarycentricDeriv
{
    float3 m_lambda;
    float3 m_ddx;
    float3 m_ddy;
};

// Perspective-correct barycentrics + analytic UV derivatives.
// From: http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
// pt0/pt1/pt2: clip-space positions; pixelNdc: NDC of current pixel (Y-up convention);
// winSize: viewport dimensions in pixels.
BarycentricDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
{
    BarycentricDeriv ret = (BarycentricDeriv)0;

    float3 invW = rcp(float3(pt0.w, pt1.w, pt2.w));

    float2 ndc0 = pt0.xy * invW.x;
    float2 ndc1 = pt1.xy * invW.y;
    float2 ndc2 = pt2.xy * invW.z;

    float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
    ret.m_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
    ret.m_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
    float ddxSum = dot(ret.m_ddx, float3(1, 1, 1));
    float ddySum = dot(ret.m_ddy, float3(1, 1, 1));

    float2 deltaVec = pixelNdc - ndc0;
    float interpInvW = invW.x + deltaVec.x * ddxSum + deltaVec.y * ddySum;
    float interpW = rcp(interpInvW);

    ret.m_lambda.x = interpW * (invW[0] + deltaVec.x * ret.m_ddx.x + deltaVec.y * ret.m_ddy.x);
    ret.m_lambda.y = interpW * (0.0f    + deltaVec.x * ret.m_ddx.y + deltaVec.y * ret.m_ddy.y);
    ret.m_lambda.z = interpW * (0.0f    + deltaVec.x * ret.m_ddx.z + deltaVec.y * ret.m_ddy.z);

    ret.m_ddx *= (2.0f / winSize.x);
    ret.m_ddy *= (2.0f / winSize.y);
    ddxSum    *= (2.0f / winSize.x);
    ddySum    *= (2.0f / winSize.y);

    ret.m_ddy *= -1.0f;
    ddySum    *= -1.0f;

    float interpW_ddx = 1.0f / (interpInvW + ddxSum);
    float interpW_ddy = 1.0f / (interpInvW + ddySum);

    ret.m_ddx = interpW_ddx * (ret.m_lambda * interpInvW + ret.m_ddx) - ret.m_lambda;
    ret.m_ddy = interpW_ddy * (ret.m_lambda * interpInvW + ret.m_ddy) - ret.m_lambda;

    return ret;
}


// ==================== Compute Shader ====================
[shader("compute")]
[numthreads(16, 16, 1)]
void VisibilityBufferBarycentricDerivative(uint3 dtid : SV_DispatchThreadID, uniform VisibilityBufferResolvePushConstant pc)
{
    if (dtid.x >= pc.extents.x || dtid.y >= pc.extents.y) {
        return;
    }

    uint2 vis = rdgTexturesUInt2[pc.visibilityBufferIndex].Load(int3(dtid.xy, 0)).xy;

    if (vis.x == VISIBILITY_BUFFER_SENTINEL && vis.y == VISIBILITY_BUFFER_SENTINEL) {
        return;
    }

    uint meshletIndex = vis.x >> 8;
    uint triangleIndexWithinMeshlet = vis.x & 0xFF;
    uint instanceIndex = vis.y;

    Instance inst = pc.instanceBuffer[instanceIndex];
    Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
    Model m = pc.modelBuffer[inst.modelIndex];

    float3 extent = prim.boundingBoxMax - prim.boundingBoxMin;

    Meshlet meshlet = pc.meshletBuffer[meshletIndex];
    uint triangleIndex = meshlet.meshletTriangleOffset + triangleIndexWithinMeshlet;

    uint packedTriangle = pc.meshletTrianglesBuffer[triangleIndex];
    uint idx0 = (packedTriangle >> 0) & 0xFF;
    uint idx1 = (packedTriangle >> 8) & 0xFF;
    uint idx2 = (packedTriangle >> 16) & 0xFF;
    uint v0i = pc.meshletVerticesBuffer[meshlet.meshletVertexOffset + idx0];
    uint v1i = pc.meshletVerticesBuffer[meshlet.meshletVertexOffset + idx1];
    uint v2i = pc.meshletVerticesBuffer[meshlet.meshletVertexOffset + idx2];
    VertexPosition v0 = pc.vertexPosBuffer[meshlet.vertexOffset + v0i];
    float3 v0pos = DecodePosition(v0, prim.boundingBoxMin, extent);
    VertexPosition v1 = pc.vertexPosBuffer[meshlet.vertexOffset + v1i];
    float3 v1pos = DecodePosition(v1, prim.boundingBoxMin, extent);
    VertexPosition v2 = pc.vertexPosBuffer[meshlet.vertexOffset + v2i];
    float3 v2pos = DecodePosition(v2, prim.boundingBoxMin, extent);

    float4x4 mvp = mul(pc.sceneData->viewProj, m.modelMatrix);
    float4 p0 = mul(mvp, float4(v0pos, 1.0));
    float4 p1 = mul(mvp, float4(v1pos, 1.0));
    float4 p2 = mul(mvp, float4(v2pos, 1.0));

    float2 pixelNDC = (float2(dtid.xy) + 0.5) / float2(pc.extents) * 2.0 - 1.0;

    BarycentricDeriv baryDeriv = CalcFullBary(p0, p1, p2, pixelNDC, float2(pc.extents));
    float3 bary = baryDeriv.m_lambda;

    VertexAttribute va0 = pc.vertexAttrBuffer[meshlet.vertexOffset + v0i];
    float2 uv0 = DecodeUV(va0);
    VertexAttribute va1 = pc.vertexAttrBuffer[meshlet.vertexOffset + v1i];
    float2 uv1 = DecodeUV(va1);
    VertexAttribute va2 = pc.vertexAttrBuffer[meshlet.vertexOffset + v2i];
    float2 uv2 = DecodeUV(va2);

    float2 uvDdx = baryDeriv.m_ddx.x * uv0 + baryDeriv.m_ddx.y * uv1 + baryDeriv.m_ddx.z * uv2;
    float2 uvDdy = baryDeriv.m_ddy.x * uv0 + baryDeriv.m_ddy.y * uv1 + baryDeriv.m_ddy.z * uv2;

    rdgStorageFloat2[pc.barycentricTargetIndex][dtid.xy] = float2(bary.x, bary.y);
    rdgStorageFloat4[pc.derivativeTargetIndex][dtid.xy] = float4(uvDdx, uvDdy);

    // Note: We could interpolate here to avoid another vertex fetch in the subsequent pass.
    //  But this shader will be large, and the barycentrics become useless.  I'm sure there are other good reasons put forward by Doom TDA
}

// ==================== Compute Shader ====================
[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeShadeDispatchBucketing(uint3 dtid : SV_DispatchThreadID, uniform ShadeBucketingPushConstant pc)
{
    if (dtid.x >= pc.extents.x || dtid.y >= pc.extents.y) {
        return;
    }

    uint2 vis = rdgTexturesUInt2[pc.visibilityBufferIndex].Load(int3(dtid.xy, 0)).xy;

    if (vis.x == VISIBILITY_BUFFER_SENTINEL && vis.y == VISIBILITY_BUFFER_SENTINEL) {
        return;
    }

    uint meshletIndex = vis.x >> 8;
    uint triangleIndexWithinMeshlet = vis.x & 0xFF;
    uint instanceIndex = vis.y;

    Instance inst = pc.instanceBuffer[instanceIndex];
    MaterialProperties material = pc.materialBuffer[inst.materialIndex];

    uint shadeBucket = material.shadingBucketIndex;
    uint lightBucket = material.lightingBucketIndex;

    // --- Wave-level reduction for shading buckets ---
    //  This optimization was shared to me by AI. I do not take credit for this wave optimization.
    //
    // Problem: every visible pixel naively does 4 global InterlockedMin/Max calls on
    // a handful of buffer slots. At 1440p this means millions of atomic RMWs all
    // hammering the same few addresses, causing severe contention (~7.5ms on a 1440p render target).
    //
    // Solution: reduce within the wave first. A wave is a group of 32 or 64 threads
    // that the GPU executes in lockstep. Wave intrinsics let us communicate between
    // those threads for free (no memory, no barrier). We compute the min/max across
    // all threads in the wave that share the same bucket, then have exactly ONE thread
    // do a single global atomic on behalf of the whole group.
    //
    // The loop "peels" one unique bucket per iteration:
    //   - Each iteration picks one bucket to process (the minimum bucket index among
    //     threads that haven't been handled yet).
    //   - All threads whose bucket matches participate together.
    //   - One thread writes the result to global memory.
    //   - Those threads mark themselves done.
    //   - Repeat until every thread in the wave is done.
    //
    // In the common case (1-2 unique materials per tile) this runs 1-2 iterations
    // and reduces global atomics from ~32 per bucket down to 1 per bucket per wave.

    bool done = false;
    while (WaveActiveAnyTrue(!done)) {
        // WaveActiveAnyTrue: returns true if ANY lane in the wave has !done == true.
        // This is the loop exit condition — we stop when every lane has finished.

        // Pick which bucket to handle this iteration. We want the bucket belonging
        // to any not-yet-done lane. WaveActiveMin finds the minimum value across all
        // active lanes; done lanes contribute ~0u (UINT_MAX) so they can never win.
        // This guarantees we always make progress: at least the lane with the minimum
        // bucket value will match and mark itself done this iteration.
        //
        // NOTE: We cannot use WaveReadLaneFirst here. That reads from the lowest-index
        // lane regardless of done status, so a finished lane with a lower index would
        // permanently broadcast its stale bucket — no undone lane would ever match it,
        // causing an infinite loop and a device lost.
        uint activeBucket = WaveActiveMin(done ? ~0u : shadeBucket);

        if (!done && shadeBucket == activeBucket) {
            // Only lanes whose shadeBucket matches activeBucket enter this branch.
            // GPU wave divergence means wave intrinsics inside here only see THESE
            // lanes — the others are on a different execution path and are excluded.
            // This gives us a free implicit mask with no extra work.

            // Compute the bounding box of all pixels in this wave that belong to
            // activeBucket. Because of divergence, these reductions only operate on
            // the matching lanes.
            uint waveMinX = WaveActiveMin(dtid.x);
            uint waveMaxX = WaveActiveMax(dtid.x + 1u);
            uint waveMinY = WaveActiveMin(dtid.y);
            uint waveMaxY = WaveActiveMax(dtid.y + 1u);

            // WaveIsFirstLane: true for the lowest-index lane currently active in
            // this branch. Elects exactly one lane to do the global atomic, using
            // the already-reduced wave-local values. All other matching lanes skip
            // the atomic entirely.
            if (WaveIsFirstLane()) {
                InterlockedMin(pc.shadeDispatchBuffer[activeBucket].minX, waveMinX);
                InterlockedMax(pc.shadeDispatchBuffer[activeBucket].maxX, waveMaxX);
                InterlockedMin(pc.shadeDispatchBuffer[activeBucket].minY, waveMinY);
                InterlockedMax(pc.shadeDispatchBuffer[activeBucket].maxY, waveMaxY);
            }

            // Mark this lane as done so it contributes ~0u to future WaveActiveMin
            // calls and doesn't interfere with remaining buckets.
            done = true;
        }
    }

    // Identical pattern for lighting buckets. Lighting buckets are coarser than
    // shading buckets (multiple materials can share one lighting shader), so there
    // are typically even fewer unique values per wave — often just 1 iteration.
    done = false;
    while (WaveActiveAnyTrue(!done)) {
        uint activeBucket = WaveActiveMin(done ? ~0u : lightBucket);
        if (!done && lightBucket == activeBucket) {
            uint waveMinX = WaveActiveMin(dtid.x);
            uint waveMaxX = WaveActiveMax(dtid.x + 1u);
            uint waveMinY = WaveActiveMin(dtid.y);
            uint waveMaxY = WaveActiveMax(dtid.y + 1u);
            if (WaveIsFirstLane()) {
                InterlockedMin(pc.lightDispatchBuffer[activeBucket].minX, waveMinX);
                InterlockedMax(pc.lightDispatchBuffer[activeBucket].maxX, waveMaxX);
                InterlockedMin(pc.lightDispatchBuffer[activeBucket].minY, waveMinY);
                InterlockedMax(pc.lightDispatchBuffer[activeBucket].maxY, waveMaxY);
            }
            done = true;
        }
    }
}

// ==================== Compute Shader ====================
[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeShadeDispatchBucketingResolve(uint3 dtid : SV_DispatchThreadID, uniform ShadeBucketingResolvePushConstant pc)
{
    uint32_t materialIndex = dtid.x;
    if (materialIndex >= pc.materialCount) {
        return;
    }

    ShadeDispatchParameters dispatchParams = pc.shadeDispatchBuffer[materialIndex];
    if (dispatchParams.minX == UINT32_MAX) {
        pc.shadeDispatchBuffer[materialIndex].xDispatch = 0;
        pc.shadeDispatchBuffer[materialIndex].yDispatch = 0;
        pc.shadeDispatchBuffer[materialIndex].zDispatch = 0;
        return;
    }

    uint32_t width = dispatchParams.maxX - dispatchParams.minX;
    uint32_t height = dispatchParams.maxY - dispatchParams.minY;

    pc.shadeDispatchBuffer[materialIndex].xDispatch = (width + 15) / 16;
    pc.shadeDispatchBuffer[materialIndex].yDispatch = (height + 15) / 16;
    pc.shadeDispatchBuffer[materialIndex].zDispatch = 1;
}

// ==================== Compute Shader ====================
[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeLightDispatchBucketingResolve(uint3 dtid : SV_DispatchThreadID, uniform LightingBucketingResolvePushConstant pc)
{
    uint32_t lightingIndex = dtid.x;
    if (lightingIndex >= pc.lightingCount) {
        return;
    }

    LightingDispatchParameters dispatchParams = pc.lightDispatchBuffer[lightingIndex];
    if (dispatchParams.minX == UINT32_MAX) {
        pc.lightDispatchBuffer[lightingIndex].xDispatch = 0;
        pc.lightDispatchBuffer[lightingIndex].yDispatch = 0;
        pc.lightDispatchBuffer[lightingIndex].zDispatch = 0;
        return;
    }

    uint32_t width = dispatchParams.maxX - dispatchParams.minX;
    uint32_t height = dispatchParams.maxY - dispatchParams.minY;

    pc.lightDispatchBuffer[lightingIndex].xDispatch = (width + 15) / 16;
    pc.lightDispatchBuffer[lightingIndex].yDispatch = (height + 15) / 16;
    pc.lightDispatchBuffer[lightingIndex].zDispatch = 1;
}

// ==================== Compute Shader ====================
[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeBucketDispatchCount(uint3 dtid : SV_DispatchThreadID, uniform BucketDispatchCountPushConstant pc)
{
    uint32_t shadeActive = 0;
    if (dtid.x < pc.materialCount) {
        shadeActive = (pc.shadeDispatchBuffer[dtid.x].xDispatch > 0) ? 1 : 0;
    }

    uint32_t lightActive = 0;
    if (dtid.x < pc.lightingCount) {
        lightActive = (pc.lightDispatchBuffer[dtid.x].xDispatch > 0) ? 1 : 0;
    }

    uint32_t waveShadeCount = WaveActiveCountBits(shadeActive != 0);
    uint32_t waveLightCount = WaveActiveCountBits(lightActive != 0);

    if (WaveIsFirstLane()) {
        if (waveShadeCount > 0) {
            InterlockedAdd(pc.countBuffer[0], waveShadeCount);
        }
        if (waveLightCount > 0) {
            InterlockedAdd(pc.countBuffer[1], waveLightCount);
        }
    }
}
