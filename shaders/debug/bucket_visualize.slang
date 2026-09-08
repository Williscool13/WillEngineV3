import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;
import shader_constants;
import bindless_declarations;

groupshared uint gsBits[MAX_SHADE_BUCKETS / 32];

static float3 BucketHue(uint bucket, bool bLighting)
{
    float hue = frac(float(bucket) * 0.61803398875 + (bLighting ? 0.5 : 0.0));
    float r = abs(hue * 6.0 - 3.0) - 1.0;
    float g = 2.0 - abs(hue * 6.0 - 2.0);
    float b = 2.0 - abs(hue * 6.0 - 4.0);
    return saturate(float3(r, g, b));
}

static const float3 HEAT_RAMP[8] = {
    float3(0.0, 0.0, 0.0),
    float3(0.0, 0.0, 1.0),
    float3(0.0, 1.0, 1.0),
    float3(0.0, 1.0, 0.0),
    float3(1.0, 1.0, 0.0),
    float3(1.0, 0.5, 0.0),
    float3(1.0, 0.0, 0.0),
    float3(1.0, 0.0, 1.0),
};

// Bucket at ordinal n (n-th lowest set bit), ~0u if fewer than n+1 set
static uint NthBucket(uint n, uint words)
{
    for (uint w = 0; w < words; w++) {
        uint bits = gsBits[w];
        uint count = countbits(bits);
        if (n < count) {
            for (uint k = 0; k < n; k++) {
                bits &= bits - 1u;
            }
            return w * 32u + firstbitlow(bits);
        }
        n -= count;
    }
    return ~0u;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeBucketDebug(uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint gindex : SV_GroupIndex, uniform BucketDebugPushConstant pc)
{
    bool bLighting = pc.bLighting != 0u;
    uint words = bLighting ? MAX_LIGHTING_BUCKETS / 32 : MAX_SHADE_BUCKETS / 32;
    uint tileLinear = gid.y * pc.tilesX + gid.x;
    if (gindex < words) {
        gsBits[gindex] = pc.tileBitsBuffer[tileLinear * words + gindex];
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.extents)) {
        return;
    }
    uint edgeDepth = min(min(gtid.x, gtid.y), min(BUCKET_TILE_SIZE - 1u - gtid.x, BUCKET_TILE_SIZE - 1u - gtid.y));

    if (pc.bHeat != 0u) {
        uint count = 0u;
        for (uint w = 0; w < words; w++) {
            count += countbits(gsBits[w]);
        }
        float3 color = HEAT_RAMP[min(count, 7u)];
        if (edgeDepth == 0u) {
            color *= 0.6;
        }
        rdgStorageFloat4[pc.outputIndex][pixel] = float4(color, 1.0);
        return;
    }

    // Ring n (inset n pixels) belongs to the n-th bucket dispatched to this tile; fill is the pixel's own bucket
    uint ringBucket = NthBucket(edgeDepth, words);
    if (ringBucket != ~0u) {
        rdgStorageFloat4[pc.outputIndex][pixel] = float4(BucketHue(ringBucket, bLighting), 1.0);
        return;
    }

    float3 color = float3(0.0);
    uint2 vis = rdgTexturesUInt2[pc.visibilityBufferIndex].Load(int3(pixel, 0)).xy;
    if (!(vis.x == VISIBILITY_BUFFER_SENTINEL && vis.y == VISIBILITY_BUFFER_SENTINEL)) {
        MaterialProperties material = pc.materialBuffer[pc.instanceBuffer[vis.y].materialIndex];
        color = BucketHue(bLighting ? material.lightingBucketIndex : material.shadingBucketIndex, bLighting) * 0.5;
    }
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(color, 1.0);
}
