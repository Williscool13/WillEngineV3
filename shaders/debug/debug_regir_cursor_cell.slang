import push_constant_interop;
import common_interop;
import constants_interop;
import restir_interop;
import lights_interop;
import bindless_declarations;
import shader_functions;
import restir_common;
import regir_hash_common;

// =============== Shader Summary ============
// Reads back the ReGIR cell under the cursor: its key, entry count, total mass and the top 8 entries by share of that mass.

static const uint CURSOR_CELL_TOP = 8u;

[[vk::push_constant]]
DebugReGIRCursorCellPushConstant pc;

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeDebugReGIRCursorCell()
{
    pc.readback->regirCursorValid = 0u;
    if (any(pc.cursorPixel >= pc.renderExtent)) { return; }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pc.cursorPixel) + 0.5) / float2(pc.renderExtent);
    float depth = rdgTexturesFloat[pc.depthTextureIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
    if (depth <= 0.0) { return; }
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPos = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
    float3 worldPos = mul(scene.invView, float4(viewPos, 1.0)).xyz;

    ReGIRHashKey key = ReGIRComputeKey(worldPos, scene.cameraWorldPos.xyz);
    uint cellSlot = ReGIRHashFind(pc.regirHashEntries, key);
    pc.readback->regirCursorLevel = key.level;
    pc.readback->regirCursorCell[0] = key.cellPos.x;
    pc.readback->regirCursorCell[1] = key.cellPos.y;
    pc.readback->regirCursorCell[2] = key.cellPos.z;
    pc.readback->regirCursorSlot = cellSlot;
    if (cellSlot == REGIR_HASH_INVALID) { return; }

    uint base = ReGIRCellEntryBase(cellSlot);
    uint2 cellInfo = pc.regirCellData[cellSlot];
    uint count = min(cellInfo.x, REGIR_ENTRIES_PER_CELL);
    float total = count > 0u ? asfloat(cellInfo.y) : 0.0;

    uint topKey[CURSOR_CELL_TOP];
    float topMass[CURSOR_CELL_TOP];
    for (uint k = 0u; k < CURSOR_CELL_TOP; k++) { topKey[k] = ~0u; topMass[k] = 0.0; }

    float prevCum = 0.0;
    for (uint e = 0u; e < count; e++) {
        ReGIREntry entry = pc.regirEntries[base + e];
        float mass = entry.cumMass - prevCum;
        prevCum = entry.cumMass;
        uint carryKey = entry.key;
        float carryMass = mass;
        for (uint k = 0u; k < CURSOR_CELL_TOP; k++) {
            if (carryMass > topMass[k]) {
                uint sk = topKey[k];
                float sm = topMass[k];
                topKey[k] = carryKey;
                topMass[k] = carryMass;
                carryKey = sk;
                carryMass = sm;
            }
        }
    }

    for (uint k = 0u; k < CURSOR_CELL_TOP; k++) {
        uint lightCount = 0u;
        float3 pos = float3(0.0);
        if (topKey[k] != ~0u) {
            if ((topKey[k] & REGIR_KEY_MESHLET) != 0u) {
                lightCount = 1u;
                if (pc.lightData != nullptr) {
                    EmissiveMeshlet meshlet = pc.lightData->emissiveMeshlets[topKey[k] & ~REGIR_KEY_MESHLET];
                    lightCount = meshlet.lightCount;
                    pos = (meshlet.aabbMin + meshlet.aabbMax) * 0.5;
                }
            }
            else {
                lightCount = 1u;
                if (pc.lightVS != nullptr) {
                    LightView vs = UnpackLightVS(pc.lightVS[topKey[k]]);
                    pos = mul(scene.invView, float4(vs.center, 1.0)).xyz;
                }
            }
        }
        pc.readback->regirCursorTopKey[k] = topKey[k];
        pc.readback->regirCursorTopShare[k] = total > 0.0 ? topMass[k] / total : 0.0;
        pc.readback->regirCursorTopLightCount[k] = lightCount;
        pc.readback->regirCursorTopPos[k * 3u + 0u] = pos.x;
        pc.readback->regirCursorTopPos[k * 3u + 1u] = pos.y;
        pc.readback->regirCursorTopPos[k * 3u + 2u] = pos.z;
    }
    pc.readback->regirCursorEntryCount = count;
    pc.readback->regirCursorTotalMass = total;
    pc.readback->regirCursorValid = 1u;
}
