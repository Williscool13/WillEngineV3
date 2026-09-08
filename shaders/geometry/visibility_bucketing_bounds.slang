import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;

import shader_constants;
import shader_functions;
import bindless_declarations;

groupshared uint gsShadeBits[MAX_SHADE_BUCKETS / 32];
groupshared uint gsLightBits[MAX_LIGHTING_BUCKETS / 32];

static bool ClaimBucket(uint bucket, bool bLighting)
{
    uint bit = 1u << (bucket & 31u);
    uint prev;
    if (bLighting) {
        InterlockedOr(gsLightBits[bucket >> 5], bit, prev);
    } else {
        InterlockedOr(gsShadeBits[bucket >> 5], bit, prev);
    }
    return (prev & bit) == 0u;
}

// ==================== Compute Shader ====================
[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeShadeDispatchBucketing(uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gindex : SV_GroupIndex, uniform ShadeBucketingPushConstant pc)
{
    if (gindex < MAX_SHADE_BUCKETS / 32) {
        gsShadeBits[gindex] = 0u;
    }
    if (gindex < MAX_LIGHTING_BUCKETS / 32) {
        gsLightBits[gindex] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    uint tileIndex = (gid.y << 16) | gid.x;

    bool valid = dtid.x < pc.extents.x && dtid.y < pc.extents.y;
    uint shadeBucket = ~0u;
    uint lightBucket = ~0u;
    if (valid) {
        uint2 vis = rdgTexturesUInt2[pc.visibilityBufferIndex].Load(int3(dtid.xy, 0)).xy;
        valid = !(vis.x == VISIBILITY_BUFFER_SENTINEL && vis.y == VISIBILITY_BUFFER_SENTINEL);
        if (valid) {
            Instance inst = pc.instanceBuffer[vis.y];
            MaterialProperties material = pc.materialBuffer[inst.materialIndex];
            shadeBucket = material.shadingBucketIndex;
            lightBucket = material.lightingBucketIndex;
        }
    }

    // Wave-level reduction: each loop iteration peels one bucket (WaveActiveMin over not-done
    // lanes; done lanes submit ~0u) and one elected lane does the append for the whole wave;
    // naive per-pixel atomics cost ~7.5ms at 1440p.
    // WaveReadLaneFirst is unusable for the peel: a done lane with a lower index would broadcast
    // its stale bucket forever and hang the loop.
    bool done = !valid;
    while (WaveActiveAnyTrue(!done)) {
        uint activeBucket = WaveActiveMin(done ? ~0u : shadeBucket);
        if (!done && shadeBucket == activeBucket) {
            if (WaveIsFirstLane() && ClaimBucket(activeBucket, false)) {
                uint slot;
                InterlockedAdd(pc.shadeDispatchBuffer[activeBucket].xDispatch, 1u, slot);
                pc.shadeTileListBuffer[activeBucket * pc.tileCapacity + slot] = tileIndex;
            }
            done = true;
        }
    }

    done = !valid;
    while (WaveActiveAnyTrue(!done)) {
        uint activeBucket = WaveActiveMin(done ? ~0u : lightBucket);
        if (!done && lightBucket == activeBucket) {
            if (WaveIsFirstLane() && ClaimBucket(activeBucket, true)) {
                uint slot;
                InterlockedAdd(pc.lightDispatchBuffer[activeBucket].xDispatch, 1u, slot);
                pc.lightTileListBuffer[activeBucket * pc.tileCapacity + slot] = tileIndex;
            }
            done = true;
        }
    }
}
