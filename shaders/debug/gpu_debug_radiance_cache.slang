import common_interop;
import model_interop;
import push_constant_interop;
import gpu_debug_draw_common;
import lights_interop;
import world_grid_interop;
import binning_common;
import packing_functions;
import radiance_cache_hash_common;
import shader_functions;
import radiance_cache_interop;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeGPUDebugRadianceCache(uint3 tid: SV_DispatchThreadID, uniform RadianceCacheDebugPushConstant pc)
{
    uint slot = tid.x;
    if (slot >= RADIANCE_CACHE_HASH_CAPACITY) { return; }
    if (pc.entries[slot] == RADIANCE_CACHE_HASH_EMPTY) { return; }

    RadianceCacheKey key = RadianceCacheUnpackKey(pc.keys[slot]);
    if (pc.bucketFilter >= 0 && int(key.normalBucket) != pc.bucketFilter) { return; }

    float halfExtent = RadianceCacheCellSizeAtLevel(key.level) * 0.45;
    float3 center = RadianceCacheCellCenterFromKey(key) + RadianceCacheBucketNormal(key.normalBucket) * halfExtent * 0.5;

    uint2 packed = pc.cells[slot].packedRadiance;
    float3 radiance = RadianceCacheRadianceShaded(packed) ? (RadianceCacheUnpackRadiance(packed) + UnpackEmissiveRGBE(pc.cells[slot].packedEmissive)) * pc.exposure : float3(0.0);
    GPUDebugAddCube(pc.cubeArgs, pc.cubeBuffer, center, halfExtent, float4(radiance, 1.0));
}
