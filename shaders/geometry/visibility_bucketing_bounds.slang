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
