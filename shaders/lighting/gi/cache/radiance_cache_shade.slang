import push_constant_interop;
import radiance_cache_hash_common;
import hit_shading_common;
import binning_common;
import packing_functions;
import shader_functions;
import random_common;
import radiance_cache_interop;
import model_interop;
import common_interop;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeRadianceCacheShade(uint3 dtid: SV_DispatchThreadID, uniform RadianceCacheShadePushConstant pc)
{
    if (dtid.x >= pc.activeCount[0]) { return; }
    uint slot = pc.activeList[dtid.x];

    RadianceCacheHitDescriptor hit = pc.descriptors[slot];
    float2 bary = UnpackUnorm2x16(hit.packedBary);
    Instance instance = pc.instanceBuffer[hit.instanceID];

    // lastShaded holds this shade event's queue frame; salting with it decorrelates events even if the hit bary repeats.
    uint rngSeed = SeedRng(uint2(slot, hit.packedBary), pc.cells[slot].lastShaded, 0x5CACEu);
    float3 emissive;
    float3 radiance = ShadeRadianceCacheCell(pc, instance, hit.primitiveIndex, bary, rngSeed, emissive);

    float maxChannel = max(radiance.r, max(radiance.g, radiance.b));
    if (pc.maxRadiance > 0.0 && maxChannel > pc.maxRadiance) {
        radiance *= pc.maxRadiance / maxChannel;
    }
    float maxEmissive = max(emissive.r, max(emissive.g, emissive.b));
    if (pc.maxRadiance > 0.0 && maxEmissive > pc.maxRadiance) {
        emissive *= pc.maxRadiance / maxEmissive;
    }

    uint2 packedPreviousRadiance = pc.cells[slot].packedRadiance;

    // Same-direction streak = real change; alternating large deltas = representative variance, keep smoothing.
    // A same-state run of penumbra picks is indistinguishable from a real step, so a dump only clamps the count: bounded move per dump, repeated dumps walk a real step across in a few shades.
    float3 blended = radiance;
    float3 blendedEmissive = emissive;
    uint streak = 0u;
    uint direction = 0u;
    uint count = 0u;
    if (RadianceCacheRadianceShaded(packedPreviousRadiance)) {
        float3 previous = RadianceCacheUnpackRadiance(packedPreviousRadiance);
        float previousLuma = dot(previous, float3(0.2126, 0.7152, 0.0722));
        float3 channelDelta = radiance - previous;
        float lumaDelta = dot(channelDelta, float3(0.2126, 0.7152, 0.0722));
        float maxAbsDelta = max(abs(channelDelta.r), max(abs(channelDelta.g), abs(channelDelta.b)));
        bool bLargeDelta = maxAbsDelta > RADIANCE_CACHE_CHANGE_THRESHOLD * max(previousLuma, 0.01);
        if (previousLuma < 0.01 && pc.stats != nullptr) {
            uint prevDark;
            InterlockedAdd(pc.stats->cellsDark, 1u, prevDark);
        }
        uint state = pc.cells[slot].changeStreak;
        uint previousStreak = state & 0xFFu;
        uint previousDirection = (state >> 8u) & 1u;
        count = min((state >> 16u) & 0xFFu, pc.accumCap - 1u);
        // Chroma-dominant deltas have no reliable luma sign; inherit the previous direction so constant-luma chromatic changes can still streak.
        direction = abs(lumaDelta) < 0.25 * maxAbsDelta ? previousDirection : (lumaDelta > 0.0 ? 1u : 0u);
        streak = bLargeDelta ? (previousStreak == 0u || direction == previousDirection ? previousStreak + 1u : 1u) : 0u;
        if (streak >= RADIANCE_CACHE_CHANGE_STREAK) {
            streak = 0u;
            count = min(count, RADIANCE_CACHE_DUMP_KEEP_COUNT);
            if (pc.stats != nullptr) {
                uint prevDumped;
                InterlockedAdd(pc.stats->cellsDumped, 1u, prevDumped);
            }
        }
        blended = lerp(radiance, previous, float(count) / float(count + 1u));
        blendedEmissive = lerp(emissive, UnpackEmissiveRGBE(pc.cells[slot].packedEmissive), float(count) / float(count + 1u));
    }
    pc.cells[slot].packedRadiance = RadianceCachePackRadiance(blended);
    pc.cells[slot].packedEmissive = PackEmissiveRGBE(blendedEmissive);
    pc.cells[slot].changeStreak = streak | (direction << 8u) | ((count + 1u) << 16u);
}
