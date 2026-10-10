import common_interop;
import push_constant_interop;
import lights_interop;
import restir_interop;
import restir_common;

groupshared uint gsKeys[TILE_LIGHT_SIZE * TILE_LIGHT_SIZE * 2u];
groupshared uint gsList[TILE_LIGHT_MAX];
groupshared uint gsCount;

static uint TileLightKey(uint lightIdx, float W, uint meshlet)
{
    if (lightIdx == ~0u || !(W > 0.0)) {
        return ~0u;
    }
    if (lightIdx < uint(MAX_ANALYTIC_LIGHTS)) {
        return lightIdx;
    }
    return meshlet == ~0u ? ~0u : (REGIR_KEY_MESHLET | meshlet);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeReSTIRTileLights(uniform ReSTIRTileLightsPushConstant pc, uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex) {
    const uint TILE_PIXELS = TILE_LIGHT_SIZE * TILE_LIGHT_SIZE;
    uint2 pixel = groupID.xy * TILE_LIGHT_SIZE + groupThreadID.xy;
    if (groupIndex == 0u) {
        gsCount = 0u;
    }

    uint keyFinal = ~0u;
    uint keyBase = ~0u;
    uint field = pc.activeCheckerboardField;
    if (all(pixel < pc.renderExtent) && IsActiveCheckerboardPixel(int2(pixel), false, field)) {
        uint idx = ReservoirIndex(PixelPosToReservoirPos(pixel, field), pc.renderExtent.x, field);
        Reservoir finalR = pc.finalBuffer[idx];
        Reservoir baseR = pc.baseBuffer[idx];
        uint meshletFinal = (finalR.lightIdx != ~0u && finalR.lightIdx >= uint(MAX_ANALYTIC_LIGHTS)) ? pc.lightData->meshletOf[finalR.lightIdx - uint(MAX_ANALYTIC_LIGHTS)] : ~0u;
        uint meshletBase = (baseR.lightIdx != ~0u && baseR.lightIdx >= uint(MAX_ANALYTIC_LIGHTS)) ? pc.lightData->meshletOf[baseR.lightIdx - uint(MAX_ANALYTIC_LIGHTS)] : ~0u;
        keyFinal = TileLightKey(finalR.lightIdx, finalR.W, meshletFinal);
        keyBase = TileLightKey(baseR.lightIdx, baseR.W, meshletBase);
    }
    gsKeys[groupIndex] = keyFinal;
    gsKeys[TILE_PIXELS + groupIndex] = keyBase;
    GroupMemoryBarrierWithGroupSync();

    for (uint s = 0u; s < 2u; s++) {
        uint slot = s * TILE_PIXELS + groupIndex;
        uint key = gsKeys[slot];
        bool bFirst = key != ~0u;
        for (uint j = 0u; j < slot && bFirst; j++) {
            bFirst = gsKeys[j] != key;
        }
        if (bFirst) {
            uint at;
            InterlockedAdd(gsCount, 1u, at);
            if (at < TILE_LIGHT_MAX) {
                gsList[at] = key;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    uint tileBase = (groupID.y * pc.tileCountX + groupID.x) * TILE_LIGHT_STRIDE;
    uint count = min(gsCount, TILE_LIGHT_MAX);
    if (groupIndex == 0u) {
        pc.tileLights[tileBase] = count;
    }
    if (groupIndex < count) {
        pc.tileLights[tileBase + 1u + groupIndex] = gsList[groupIndex];
    }
}
