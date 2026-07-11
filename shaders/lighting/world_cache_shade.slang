import push_constant_interop;
import world_cache_hash;
import hit_shading_common;
import binning_common;
import packing_functions;
import shader_functions;
import world_cache_interop;
import model_interop;
import common_interop;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeWorldCacheShade(uint3 dtid: SV_DispatchThreadID, uniform WorldCacheShadePushConstant pc)
{
    uint slot = dtid.x;
    if (slot >= WORLD_CACHE_HASH_CAPACITY) { return; }
    if (pc.active[slot] == 0u) { return; }

    WorldCacheHitDescriptor hit = pc.descriptors[slot];
    float2 bary = UnpackUnorm2x16(hit.packedBary);
    Instance instance = pc.instanceBuffer[hit.instanceID];

    WorldGridPunctualLights worldGridLights = (WorldGridPunctualLights)0;
    if (pc.worldGridBuffer != nullptr) {
        SceneData scene = pc.sceneData[pc.sceneDataIndex];
        worldGridLights.view = scene.view;
        worldGridLights.camPos = scene.cameraWorldPos.xyz;
        worldGridLights.grid = pc.worldGridBuffer;
        worldGridLights.indexList = pc.worldGridIndexList;
    }

    float3 indirectDiffuse;
    float3 radiance = ShadeWorldCacheCell(pc, instance, hit.primitiveIndex, bary, worldGridLights, indirectDiffuse);

    float maxChannel = max(radiance.r, max(radiance.g, radiance.b));
    if (pc.maxRadiance > 0.0 && maxChannel > pc.maxRadiance) {
        radiance *= pc.maxRadiance / maxChannel;
    }

    uint packedPrevious = pc.cells[slot].packedRadiance;
    float3 blended = radiance;
    if (packedPrevious != WORLD_CACHE_RADIANCE_UNSHADED) {
        float3 previous = UnpackEmissiveRGBE(packedPrevious);
        blended = lerp(radiance, previous, WORLD_CACHE_RADIANCE_HYSTERESIS);
    }
    pc.cells[slot].packedRadiance = min(PackEmissiveRGBE(blended), WORLD_CACHE_RADIANCE_UNSHADED - 1u);

    uint packedPreviousIrradiance = pc.cells[slot].packedIrradiance;
    float3 blendedIrradiance = indirectDiffuse;
    if (packedPreviousIrradiance != WORLD_CACHE_RADIANCE_UNSHADED) {
        blendedIrradiance = lerp(indirectDiffuse, UnpackEmissiveRGBE(packedPreviousIrradiance), WORLD_CACHE_RADIANCE_HYSTERESIS);
    }
    pc.cells[slot].packedIrradiance = min(PackEmissiveRGBE(blendedIrradiance), WORLD_CACHE_RADIANCE_UNSHADED - 1u);
}
