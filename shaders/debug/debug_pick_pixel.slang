import push_constant_interop;
import common_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;

[[vk::push_constant]]
DebugPickPixelPushConstant pc;

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeDebugPickPixel()
{
    pc.readback->pickValid = 0u;
    pc.readback->pickRequestId = pc.requestId;
    pc.readback->pickInstanceIndex = ~0u;
    pc.readback->pickMeshletIndex = ~0u;
    pc.readback->pickTriangleIndex = ~0u;
    if (any(pc.pickPixel >= pc.renderExtent)) { return; }

    uint2 vis = rdgTexturesUInt2[pc.visibilityTextureIndex].Load(int3(pc.pickPixel, 0)).xy;
    if (vis.x == VISIBILITY_BUFFER_SENTINEL && vis.y == VISIBILITY_BUFFER_SENTINEL) { return; }

    uint meshletIndex = vis.x >> 8;
    uint triangleIndexWithinMeshlet = vis.x & 0xFF;
    uint instanceIndex = vis.y;

    pc.readback->pickMeshletIndex = meshletIndex;
    pc.readback->pickTriangleIndex = triangleIndexWithinMeshlet;
    pc.readback->pickInstanceIndex = instanceIndex;

    float depth = rdgTexturesFloat[pc.depthTextureIndex].Load(int3(pc.pickPixel, 0));
    if (depth > 0.0) {
        SceneData scene = pc.sceneData[pc.sceneDataIndex];
        float2 uv = (float2(pc.pickPixel) + 0.5) / float2(pc.renderExtent);
        float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float3 viewPos = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
        float3 worldPos = mul(scene.invView, float4(viewPos, 1.0)).xyz;
        pc.readback->pickViewDepth = viewSpaceDepth;
        pc.readback->pickWorldPos[0] = worldPos.x;
        pc.readback->pickWorldPos[1] = worldPos.y;
        pc.readback->pickWorldPos[2] = worldPos.z;
    }

    pc.readback->pickValid = 1u;
}
