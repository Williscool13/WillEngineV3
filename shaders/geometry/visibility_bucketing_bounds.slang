import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;

import shader_constants;
import shader_functions;
import bindless_declarations;

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

    // Wave-level reduction: each loop iteration peels one bucket (WaveActiveMin over not-done
    // lanes; done lanes submit ~0u) and one elected lane does the global atomics for the whole
    // wave; naive per-pixel InterlockedMin/Max contention cost ~7.5ms at 1440p.
    // WaveReadLaneFirst is unusable for the peel: a done lane with a lower index would broadcast
    // its stale bucket forever and hang the loop.
    bool done = false;
    while (WaveActiveAnyTrue(!done)) {
        uint activeBucket = WaveActiveMin(done ? ~0u : shadeBucket);

        if (!done && shadeBucket == activeBucket) {
            uint waveMinX = WaveActiveMin(dtid.x);
            uint waveMaxX = WaveActiveMax(dtid.x + 1u);
            uint waveMinY = WaveActiveMin(dtid.y);
            uint waveMaxY = WaveActiveMax(dtid.y + 1u);

            if (WaveIsFirstLane()) {
                InterlockedMin(pc.shadeDispatchBuffer[activeBucket].minX, waveMinX);
                InterlockedMax(pc.shadeDispatchBuffer[activeBucket].maxX, waveMaxX);
                InterlockedMin(pc.shadeDispatchBuffer[activeBucket].minY, waveMinY);
                InterlockedMax(pc.shadeDispatchBuffer[activeBucket].maxY, waveMaxY);
            }

            done = true;
        }
    }

    // Same reduction for lighting buckets (coarser than shading buckets, usually 1 iteration).
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
