import common_interop;
import push_constant_interop;
import bindless_declarations;
import instance_mask_interop;

import shader_functions;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeRTShadowTest(uint3 dtid : SV_DispatchThreadID, uniform RTShadowTestPushConstant pc)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0));

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float3 worldPos = ReconstructWorldPosition(scene.invViewProj, uv, depth);
    float3 cameraPos = scene.cameraWorldPos.xyz;
    float3 rayDir = normalize(worldPos - cameraPos);

    RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];

    RayDesc ray;
    ray.Origin = cameraPos;
    ray.Direction = rayDir;
    ray.TMin = 0.0001;
    ray.TMax = length(worldPos - cameraPos) + 0.1;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, ray);
    q.Proceed();

    float4 result = float4(0.0, 0.0, 0.0, 1.0);
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        uint instanceID = q.CommittedInstanceID();
        uint h = (instanceID + 1u) * 2654435761u;
        result.rgb = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
    }
    rdgStorageFloat4[pc.outputIndex][pixel] = result;
}
