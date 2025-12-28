import common_interop;
import model_interop;
import shader_functions;
// =============== Compute Shader (Instance Visibility and Primitive Accumulation Count) ===============


struct PrimitiveCount
{
    Atomic<uint32_t> count;
    uint32_t offset;
};


struct VisibilityPushConstant {
    // Read-Only
    SceneData* sceneData;
    MeshletPrimitive* primitiveBuffer;
    Model* modelBuffer;
    Instance* instanceBuffer;

    // Read-Write
    uint32_t* packedVisibilityBuffer;
    uint32_t* instanceOffsetBuffer;
    PrimitiveCount* primitiveCountBuffer;
};

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

        uint32_t primitiveOffset = pushConstant.primitiveCountBuffer[inst.primitiveIndex].count.add(1);
        // Warning: Narrowing, if instanceCount ever intends on exceeding 65536, need to change primitiveOffset to uint32_t
        pushConstant.instanceOffsetBuffer[instanceID] = primitiveOffset;
    }
}

// =============== Compute Shader (Accumulation to determine offset of each instance) ===============
// Note: This is a simple single threaded prefix sum implementation. It is unlikely to be the real bottleneck of the packed instance buffer generation.
//     If needed, expand this with multi-pass complex GPU prefix sum and intermediate buffers.
struct PrefixSumPushConstant {
    // Read-Write
    PrimitiveCount* primitiveCountBuffer;

    // Read-Only
    uint32_t highestPrimitiveIndex;
};

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
struct InstancedMeshIndirectDrawParameters {
    // indirect parameters
    uint32_t groupCountX;
    uint32_t groupCountY; // offset
    uint32_t groupCountZ;
    uint32_t compactedInstanceStart;

    uint32_t meshletOffset;
    uint32_t meshletCount;
    uint32_t padding1;
    uint32_t padding2;
};

struct InstancedMeshIndirectParameterBuffer {
    Atomic<uint32_t> meshIndirectCount;
    uint32_t padding0;
    uint32_t padding1;
    uint32_t padding2;
    InstancedMeshIndirectDrawParameters[] parameters;
}

struct IndirectWritePushConstant {
    // Read-Only
    SceneData* sceneData;
    MeshletPrimitive* primitiveBuffer;
    Model* modelBuffer;
    Instance* instanceBuffer;

    uint32_t* packedVisibilityBuffer;
    uint32_t* instanceOffsetBuffer;
    PrimitiveCount* primitiveCountBuffer;
    // Read-Write
    // todo: packed doesn't need bIsAllocated, can be optimized
    Instance* compactedInstanceBuffer;
    InstancedMeshIndirectParameterBuffer* indirectBuffer;
};

const static int32_t TASK_SHADER_DISPATCH_X = 64;

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
    uint32_t primitiveOffset = pushConstant.instanceOffsetBuffer[instanceID];
    uint32_t globalOffset = pushConstant.primitiveCountBuffer[inst.primitiveIndex].offset;

    pushConstant.compactedInstanceBuffer[globalOffset + primitiveOffset] = inst;

    if (primitiveOffset == 0) {
        MeshletPrimitive prim = pushConstant.primitiveBuffer[inst.primitiveIndex];
        uint totalMeshlets = prim.meshletCount;
        uint numChunks = (totalMeshlets + (TASK_SHADER_DISPATCH_X - 1)) / TASK_SHADER_DISPATCH_X;
        uint32_t count = pushConstant.primitiveCountBuffer[inst.primitiveIndex].count.load();

        InstancedMeshIndirectDrawParameters parameters;
        parameters.groupCountX = numChunks;
        parameters.groupCountY = count;
        parameters.groupCountZ = 1;
        parameters.compactedInstanceStart = globalOffset;

        parameters.meshletOffset = prim.meshletOffset;
        parameters.meshletCount = prim.meshletCount;

        uint32_t indirectIndex = pushConstant.indirectBuffer->meshIndirectCount.add(1);
        pushConstant.indirectBuffer->parameters[indirectIndex] = parameters;
    }
}
