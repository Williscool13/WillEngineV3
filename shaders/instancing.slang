import common_interop;
import model_interop;
import shader_functions;
import instancing_interop;
import push_constant_interop;
import constants_interop;
// =============== Compute Shader (Instance Visibility and Primitive Accumulation Count) ===============
// Evaluates the visibility of each instance and outputs to a packed visibility buffer
[shader("compute")]
[numthreads(64, 1, 1)]
void computeVisibility(uniform VisibilityPushConstant pushConstant, uint3 dtid : SV_DispatchThreadID)
{
    uint instanceID = dtid.x;

    Instance inst = pushConstant.instanceBuffer[instanceID];
    if (inst.bIsAllocated == 0) { return; }

    MeshletPrimitive prim = pushConstant.primitiveBuffer[inst.primitiveIndex];
    Model model = pushConstant.modelBuffer[inst.modelIndex];

    float4 boundingSphere = prim.boundingSphere;
    float3 worldCenter = mul(model.modelMatrix, float4(boundingSphere.xyz, 1.0)).xyz;
    float4 worldBounds = float4(worldCenter, boundingSphere.w);

    bool bVisible = true;
    if (!frustumCullSphere(pushConstant.sceneData->frustum, worldBounds)) {
        bVisible = false;
    }

    // set visibility bit (uint32_t chunks)
    int visibilityChunkIndex = instanceID / 32;
    int visibilityChunkBit = instanceID % 32;

    if (bVisible) {
        uint32_t bitMask = 1u << visibilityChunkBit;
        InterlockedOr(pushConstant.packedVisibilityBuffer[visibilityChunkIndex], bitMask);

        uint32_t instanceOffset = pushConstant.primitiveCountBuffer[inst.primitiveIndex].count.add(1);
        pushConstant.instanceOffsetBuffer[instanceID] = instanceOffset;
    }
}

// =============== Compute Shader (Accumulation to determine offset of each instance) ===============
// Note: This is a simple single threaded prefix sum implementation. It is unlikely to be the real bottleneck of the packed instance buffer generation.
//     If needed, expand this with multi-pass complex GPU prefix sum and intermediate buffers.

[shader("compute")]
[numthreads(1, 1, 1)]
void computePrefixSum(uniform PrefixSumPushConstant pushConstant) {
    uint sum = 0;
    for (uint i = 0; i < pushConstant.highestPrimitiveIndex; i++) {
        pushConstant.primitiveCountBuffer[i].offset = sum;
        sum += pushConstant.primitiveCountBuffer[i].count.load();
    }
}

// =============== Compute Shader (Instance Draw buffer write) ===============
// Each thread operates on 1 instance. The 0th instance of a primitive will populate the indirect

[shader("compute")]
[numthreads(64, 1, 1)]
void computeCompactAndGenerateIndirect(uniform IndirectWritePushConstant pushConstant, uint3 dtid : SV_DispatchThreadID)
{
    uint instanceID = dtid.x;

    uint visChunkIndex = instanceID / 32;
    uint visChunkBit = instanceID % 32;
    uint32_t visChunk = pushConstant.packedVisibilityBuffer[visChunkIndex];
    bool visible = (visChunk & (1u << visChunkBit)) != 0;

    if (!visible) return;

    Instance inst = pushConstant.instanceBuffer[instanceID];

    // Offset to 0th primitive of a given primitive
    uint32_t primitiveOffset = pushConstant.primitiveCountBuffer[inst.primitiveIndex].offset;
    // Instance offset within primitive group
    uint32_t instanceOffset = pushConstant.instanceOffsetBuffer[instanceID];

    pushConstant.compactedInstanceBuffer[primitiveOffset + instanceOffset].primitiveIndex = inst.primitiveIndex;
    pushConstant.compactedInstanceBuffer[primitiveOffset + instanceOffset].modelIndex = inst.modelIndex;
    pushConstant.compactedInstanceBuffer[primitiveOffset + instanceOffset].materialIndex = inst.materialIndex;
    pushConstant.compactedInstanceBuffer[primitiveOffset + instanceOffset].jointMatrixOffset = inst.jointMatrixOffset;

    if (primitiveOffset == 0) {
        MeshletPrimitive prim = pushConstant.primitiveBuffer[inst.primitiveIndex];
        uint totalMeshlets = prim.meshletCount;
        uint numChunks = (totalMeshlets + (TASK_SHADER_DISPATCH_X - 1)) / TASK_SHADER_DISPATCH_X;
        uint32_t count = pushConstant.primitiveCountBuffer[inst.primitiveIndex].count.load();

        InstancedMeshIndirectDrawParameters parameters;
        parameters.groupCountX = numChunks;
        parameters.groupCountY = count;
        parameters.groupCountZ = 1;
        parameters.compactedInstanceStart = primitiveOffset;

        parameters.meshletOffset = prim.meshletOffset;
        parameters.meshletCount = prim.meshletCount;

        uint32_t indirectIndex = pushConstant.indirectCountBuffer->meshIndirectCount.add(1);
        pushConstant.indirectBuffer[indirectIndex] = parameters;
    }
}
