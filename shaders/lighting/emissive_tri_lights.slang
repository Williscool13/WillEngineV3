import common_interop;
import model_interop;
import lights_interop;
import push_constant_interop;
import shader_functions;

// =============== Shader Summary ============
// One workgroup per dirty emissive primitive instance. Writes a LightInfo per triangle in BLAS index order (base + PrimitiveIndex() resolves a hit to its light), an EmissiveMeshlet per LOD0 meshlet
// (the index buffer is baked in meshlet order, so each is a contiguous run), the owning EmissiveMesh, meshletOf per triangle, and a mesh-wide power prefix in LightInfo.up.w.
// A dead item writes zero lights and inverted AABBs.

static const uint TRI_LIGHT_GROUP_SIZE = 64;

groupshared float3 gsMin[TRI_LIGHT_GROUP_SIZE];
groupshared float3 gsMax[TRI_LIGHT_GROUP_SIZE];
groupshared float gsPower[TRI_LIGHT_GROUP_SIZE];
groupshared float gsMeshPower;

static uint PackColorRGB8(float3 c)
{
    uint3 q = uint3(saturate(c) * 255.0 + 0.5);
    return q.x | (q.y << 8) | (q.z << 16) | (255u << 24);
}

static EmissiveMeshlet DeadMeshlet(uint firstLight)
{
    EmissiveMeshlet meshlet;
    meshlet.firstLight = firstLight;
    meshlet.lightCount = 0u;
    meshlet.power = 0.0;
    meshlet.rangePad = 0.0;
    meshlet.packedConeAxis = 0u;
    meshlet.coneCutoff = 1.0;
    meshlet.aabbMin = float3(1e30);
    meshlet.aabbMax = float3(-1e30);
    return meshlet;
}

static void WriteDeadMesh(EmissiveTriLightPushConstant pc, EmissiveTriLightWork work, uint lane)
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
        pc.lightData->meshletOf[work.firstLight + t - uint(MAX_ANALYTIC_LIGHTS)] = ~0u;
    }

    // Zero counts so a released range that gets reused does not also resolve to these stale slots
    for (uint c = lane; c < work.meshletCount; c += TRI_LIGHT_GROUP_SIZE) {
        pc.lightData->emissiveMeshlets[work.firstMeshlet + c] = DeadMeshlet(work.firstLight);
    }

    if (lane != 0) { return; }
    EmissiveMesh mesh;
    mesh.firstMeshlet = work.firstMeshlet;
    mesh.meshletCount = 0u;
    mesh.firstLight = work.firstLight;
    mesh.lightCount = 0u;
    mesh.power = 0.0;
    mesh.rangePad = 0.0;
    mesh.aabbMin = float3(1e30);
    mesh.aabbMax = float3(-1e30);
    pc.lightData->emissiveMeshes[work.meshSlot] = mesh;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEmissiveTriLights(uniform EmissiveTriLightPushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID)
{
    uint lane = gtid.x;
    if (gid.x >= pc.workCount) { return; }

    EmissiveTriLightWork work = pc.workBuffer[gid.x];
    if (work.bDead != 0u) {
        WriteDeadMesh(pc, work, lane);
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

    // The store falls back to one EmissiveMeshlet for the whole run when its pool is exhausted
    bool bPerMeshlet = work.meshletCount == uint(prim.meshletCount.x);

    // Non-uniform scale reshapes the normal cone and mirroring flips the cross(e1, e2) light normals against it
    float3 axisX = mul(m3, float3(1.0, 0.0, 0.0));
    float3 axisY = mul(m3, float3(0.0, 1.0, 0.0));
    float3 axisZ = mul(m3, float3(0.0, 0.0, 1.0));
    float scaleMin = min(length(axisX), min(length(axisY), length(axisZ)));
    float scaleMax = max(length(axisX), max(length(axisY), length(axisZ)));
    bool bConeValid = bPerMeshlet && scaleMin > 1e-6 && scaleMax <= scaleMin * 1.01 && determinant(m3) > 0.0;

    float3 meshMin = float3(1e30);
    float3 meshMax = float3(-1e30);
    float meshPower = 0.0;
    uint triBase = 0u;
    for (uint c = 0u; c < work.meshletCount; c++) {
        uint triCount = bPerMeshlet ? pc.meshletBuffer[uint(prim.meshletOffset.x) + c].meshletTriangleCount : work.triangleCount;

        float3 laneMin = float3(1e30);
        float3 laneMax = float3(-1e30);
        float lanePower = 0.0;

        for (uint t = lane; t < triCount; t += TRI_LIGHT_GROUP_SIZE) {
            uint tri = triBase + t;
            uint firstIndex = prim.indexOffset + tri * 3;
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
            pc.lightData->lights[work.firstLight + tri] = light;
            pc.lightData->meshletOf[work.firstLight + tri - uint(MAX_ANALYTIC_LIGHTS)] = work.firstMeshlet + c;
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

        if (lane == 0) {
            // Unpadded; the range pad needs the whole emitter's power and is applied below
            EmissiveMeshlet meshlet;
            meshlet.firstLight = work.firstLight + triBase;
            meshlet.lightCount = triCount;
            meshlet.power = gsPower[0];
            meshlet.rangePad = 0.0;
            meshlet.packedConeAxis = 0u;
            meshlet.coneCutoff = 1.0;
            if (bConeValid) {
                Meshlet mlet = pc.meshletBuffer[uint(prim.meshletOffset.x) + c];
                meshlet.packedConeAxis = PackNormalOct16(normalize(mul(m3, mlet.coneAxis)));
                meshlet.coneCutoff = mlet.coneCutoff;
            }
            meshlet.aabbMin = gsMin[0];
            meshlet.aabbMax = gsMax[0];
            pc.lightData->emissiveMeshlets[work.firstMeshlet + c] = meshlet;
            meshMin = min(meshMin, gsMin[0]);
            meshMax = max(meshMax, gsMax[0]);
            meshPower += gsPower[0];
        }
        GroupMemoryBarrierWithGroupSync();
        triBase += triCount;
    }

    if (lane == 0) { gsMeshPower = meshPower; }
    AllMemoryBarrierWithGroupSync();

    if (lane == 0) {
        float cum = 0.0;
        for (uint c = 0u; c < work.meshletCount; c++) {
            uint first = pc.lightData->emissiveMeshlets[work.firstMeshlet + c].firstLight;
            uint count = pc.lightData->emissiveMeshlets[work.firstMeshlet + c].lightCount;
            float cumBase = cum;
            for (uint t = 0u; t < count; t++) {
                float3 e1 = pc.lightData->lights[first + t].right.xyz;
                float3 e2 = pc.lightData->lights[first + t].up.xyz;
                cum += pc.lightData->lights[first + t].intensity * 0.5 * length(cross(e1, e2));
                pc.lightData->lights[first + t].up.w = cum;
            }
            pc.lightData->emissiveMeshlets[work.firstMeshlet + c].power = cum - cumBase;
        }
    }
    AllMemoryBarrierWithGroupSync();

    // Range is a property of the whole emitter, not of its tessellation: per-triangle sqrt(intensity * area) shrinks by sqrt(N) as a mesh is subdivided.
    float meshRange = pc.rangeMultiplier * sqrt(max(gsMeshPower, 0.0));
    for (uint t = lane; t < work.triangleCount; t += TRI_LIGHT_GROUP_SIZE) {
        pc.lightData->lights[work.firstLight + t].range = meshRange;
    }
    for (uint c = lane; c < work.meshletCount; c += TRI_LIGHT_GROUP_SIZE) {
        EmissiveMeshlet meshlet = pc.lightData->emissiveMeshlets[work.firstMeshlet + c];
        if (meshlet.power > 0.0) {
            meshlet.aabbMin -= meshRange;
            meshlet.aabbMax += meshRange;
            meshlet.rangePad = meshRange;
        }
        else {
            meshlet = DeadMeshlet(meshlet.firstLight);
        }
        pc.lightData->emissiveMeshlets[work.firstMeshlet + c] = meshlet;
    }

    if (lane != 0) { return; }

    EmissiveMesh mesh;
    mesh.firstMeshlet = work.firstMeshlet;
    mesh.meshletCount = work.meshletCount;
    mesh.firstLight = work.firstLight;
    mesh.lightCount = work.triangleCount;
    mesh.power = meshPower;
    mesh.rangePad = meshPower > 0.0 ? meshRange : 0.0;
    if (meshPower > 0.0) {
        mesh.aabbMin = meshMin - meshRange;
        mesh.aabbMax = meshMax + meshRange;
    }
    else {
        mesh.aabbMin = float3(1e30);
        mesh.aabbMax = float3(-1e30);
    }
    pc.lightData->emissiveMeshes[work.meshSlot] = mesh;
}
