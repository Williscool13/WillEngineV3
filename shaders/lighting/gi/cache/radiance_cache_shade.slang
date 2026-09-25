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
    float3 radiance = ShadeRadianceCacheCell(pc, instance, hit.primitiveIndex, bary, hit.bMirror != 0u, rngSeed, emissive);

    float maxChannel = max(radiance.r, max(radiance.g, radiance.b));
    if (pc.maxRadiance > 0.0 && maxChannel > pc.maxRadiance) {
        radiance *= pc.maxRadiance / maxChannel;
    }
    float maxEmissive = max(emissive.r, max(emissive.g, emissive.b));
    if (pc.maxRadiance > 0.0 && maxEmissive > pc.maxRadiance) {
        emissive *= pc.maxRadiance / maxEmissive;
    }

    uint2 packedPreviousRadiance = pc.cells[slot].packedRadiance;

    float3 blended = radiance;
    float3 blendedEmissive = emissive;
    uint count = 0u;
    if (RadianceCacheRadianceShaded(packedPreviousRadiance)) {
        count = min((pc.cells[slot].changeStreak >> 16u) & 0xFFu, pc.accumCap - 1u);
        blended = lerp(radiance, RadianceCacheUnpackRadiance(packedPreviousRadiance), float(count) / float(count + 1u));
        blendedEmissive = lerp(emissive, RadianceCacheUnpackEmissive(pc.cells[slot].packedEmissive), float(count) / float(count + 1u));
    }
    float maxBlended = max(blended.r, max(blended.g, blended.b));
    if (pc.maxRadiance > 0.0 && maxBlended > pc.maxRadiance) {
        blended *= pc.maxRadiance / maxBlended;
    }
    float maxBlendedEmissive = max(blendedEmissive.r, max(blendedEmissive.g, blendedEmissive.b));
    if (pc.maxRadiance > 0.0 && maxBlendedEmissive > pc.maxRadiance) {
        blendedEmissive *= pc.maxRadiance / maxBlendedEmissive;
    }
    if (any(isnan(blended)) || any(isnan(blendedEmissive))) {
        blended = radiance;
        blendedEmissive = emissive;
    }
    pc.cells[slot].packedRadiance = RadianceCachePackRadiance(blended);
    pc.cells[slot].packedEmissive = PackEmissiveRGBE(blendedEmissive);
    pc.cells[slot].changeStreak = (count + 1u) << 16u;
}
