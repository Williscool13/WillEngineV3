import common_interop;
import model_interop;
import lights_interop;
import push_constant_interop;
import shader_functions;

// =============== Shader Summary ============
// One workgroup per dirty emissive primitive instance, writing a LightInfo per triangle in BLAS index order so base + PrimitiveIndex() resolves a hit to its own light, plus EmissiveGroup[groupSlot]. Untouched groups keep what an earlier frame wrote into this LightData slot. A dead item writes zero lights and an inverted AABB, which the grid rejects.

static const uint TRI_LIGHT_GROUP_SIZE = 64;

groupshared float3 gsMin[TRI_LIGHT_GROUP_SIZE];
groupshared float3 gsMax[TRI_LIGHT_GROUP_SIZE];
groupshared float gsPower[TRI_LIGHT_GROUP_SIZE];

static uint PackColorRGB8(float3 c)
{
    uint3 q = uint3(saturate(c) * 255.0 + 0.5);
    return q.x | (q.y << 8) | (q.z << 16) | (255u << 24);
}

static void WriteDeadGroup(EmissiveTriLightPushConstant pc, EmissiveTriLightWork work, uint lane)
{
    LightInfo light;
    light.position = float4(0.0);
    light.normal = float4(0.0);
    light.right = float4(0.0);
    light.up = float4(0.0);
    light.packedColor = 0u;
    light.intensity = 0.0;
    light.range = 0.0;
    light.type = 0u;
    for (uint t = lane; t < work.triangleCount; t += TRI_LIGHT_GROUP_SIZE) {
        pc.lightData->lights[work.firstLight + t] = light;
    }

    if (lane != 0) { return; }
    // Zero count so a released range that gets reused does not also resolve to this stale group
    EmissiveGroup group;
    group.firstLight = work.firstLight;
    group.lightCount = 0u;
    group.power = 0.0;
    group.aabbMin = float3(1e30);
    group.aabbMax = float3(-1e30);
    pc.lightData->emissiveGroups[work.groupSlot] = group;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEmissiveTriLights(uniform EmissiveTriLightPushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID)
{
    uint lane = gtid.x;
    if (gid.x >= pc.workCount) { return; }

    EmissiveTriLightWork work = pc.workBuffer[gid.x];
    if (work.bDead != 0u) {
        WriteDeadGroup(pc, work, lane);
        return;
    }

    Instance inst = pc.instanceBuffer[work.instanceSlot];
    Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
    float4x4 m = pc.modelBuffer[inst.modelIndex].modelMatrix;
    float3x3 m3 = (float3x3)m;

    float4 emissiveFactor = pc.materialBuffer[inst.materialIndex].emissiveFactor;
    float maxEmissive = max(emissiveFactor.r, max(emissiveFactor.g, emissiveFactor.b));
    float intensity = emissiveFactor.w * maxEmissive;
    uint packedColor = maxEmissive > 0.0 ? PackColorRGB8(emissiveFactor.rgb / maxEmissive) : 0u;

    float3 boundsMin = prim.boundingBoxMin;
    float3 boundsExtent = prim.boundingBoxMax - prim.boundingBoxMin;

    float3 laneMin = float3(1e30);
    float3 laneMax = float3(-1e30);
    float lanePower = 0.0;

    for (uint t = lane; t < work.triangleCount; t += TRI_LIGHT_GROUP_SIZE) {
        uint firstIndex = prim.indexOffset + t * 3;
        float3 p0 = DecodePosition(pc.vertexPosBuffer[pc.indexBuffer[firstIndex + 0]], boundsMin, boundsExtent);
        float3 p1 = DecodePosition(pc.vertexPosBuffer[pc.indexBuffer[firstIndex + 1]], boundsMin, boundsExtent);
        float3 p2 = DecodePosition(pc.vertexPosBuffer[pc.indexBuffer[firstIndex + 2]], boundsMin, boundsExtent);

        float3 v0 = mul(m, float4(p0, 1.0)).xyz;
        float3 e1 = mul(m3, p1 - p0);
        float3 e2 = mul(m3, p2 - p0);
        float3 cr = cross(e1, e2);
        float area = 0.5 * length(cr);
        // Degenerate triangles still occupy a slot (zero intensity) so base + PrimitiveIndex() stays aligned
        bool bDegenerate = area <= 1e-8 || intensity <= 0.0;

        if (!bDegenerate) {
            laneMin = min(laneMin, min(v0, min(v0 + e1, v0 + e2)));
            laneMax = max(laneMax, max(v0, max(v0 + e1, v0 + e2)));
            lanePower += intensity * area;
        }

        LightInfo light;
        light.position = float4(v0, 0.0);
        light.normal = float4(bDegenerate ? float3(0.0, 1.0, 0.0) : cr / (2.0 * area), 0.0);
        light.right = float4(e1, 0.0);
        light.up = float4(e2, 0.0);
        light.packedColor = packedColor;
        light.intensity = bDegenerate ? 0.0 : intensity;
        light.range = 0.0;
        light.type = LIGHT_TYPE_TRIANGLE;
        pc.lightData->lights[work.firstLight + t] = light;
    }

    gsMin[lane] = laneMin;
    gsMax[lane] = laneMax;
    gsPower[lane] = lanePower;
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = TRI_LIGHT_GROUP_SIZE / 2; stride > 0; stride >>= 1) {
        if (lane < stride) {
            gsMin[lane] = min(gsMin[lane], gsMin[lane + stride]);
            gsMax[lane] = max(gsMax[lane], gsMax[lane + stride]);
            gsPower[lane] += gsPower[lane + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Range is a property of the whole emitter, not of its tessellation: per-triangle sqrt(intensity * area) shrinks by sqrt(N) as a mesh is subdivided.
    float groupRange = pc.rangeMultiplier * sqrt(max(gsPower[0], 0.0));
    for (uint t = lane; t < work.triangleCount; t += TRI_LIGHT_GROUP_SIZE) {
        pc.lightData->lights[work.firstLight + t].range = groupRange;
    }

    if (lane != 0) { return; }

    EmissiveGroup group;
    group.firstLight = work.firstLight;
    group.lightCount = work.triangleCount;
    group.power = gsPower[0];
    if (gsPower[0] > 0.0) {
        group.aabbMin = gsMin[0] - groupRange;
        group.aabbMax = gsMax[0] + groupRange;
    }
    else {
        group.aabbMin = float3(1e30);
        group.aabbMax = float3(-1e30);
    }
    pc.lightData->emissiveGroups[work.groupSlot] = group;
}
