//
// Created by William on 2025-12-28.
//

#ifndef WILL_ENGINE_PUSH_CONSTANT_INTEROP_H
#define WILL_ENGINE_PUSH_CONSTANT_INTEROP_H

#ifdef __SLANG__
module push_constant_interop;
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#define SHADER_ALIGN
#define SHADER_PTR(T) T*
#define SHADER_ATOMIC(T) Atomic<T>
import common_interop;
import model_interop;
import constants_interop;
import instancing_interop;
#else
#include <glm/glm.hpp>
#include <cstdint>
#include "common_interop.h"
#include "model_interop.h"
#include "instancing_interop.h"

using uint = uint32_t;
using int32 = int32_t;
using uint32 = uint32_t;

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;

using int2 = glm::ivec2;
using int3 = glm::ivec3;
using int4 = glm::ivec4;

using uint2 = glm::uvec2;
using uint3 = glm::uvec3;
using uint4 = glm::uvec4;

using float2x2 = glm::mat2;
using float3x3 = glm::mat3;
using float4x4 = glm::mat4;

#define SHADER_PUBLIC
#define SHADER_CONST constexpr inline
#define SHADER_ALIGN alignas(16)
#define SHADER_PTR(T) VkDeviceAddress
#define SHADER_ATOMIC(T) T
#endif // __SLANG__

SHADER_PUBLIC struct DebugVisualizePushConstant
{
    SHADER_PUBLIC int2 extent;
    SHADER_PUBLIC float nearPlane;
    SHADER_PUBLIC float farPlane;
    SHADER_PUBLIC uint textureIndex;
    SHADER_PUBLIC uint samplerIndex;
    SHADER_PUBLIC uint outputImageIndex;
    SHADER_PUBLIC uint debugType;
};

SHADER_PUBLIC struct VisibilityPushConstant
{
    // Read-Only
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(MeshletPrimitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;

    // Read-Write
    SHADER_PUBLIC SHADER_PTR(uint32_t) packedVisibilityBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) instanceOffsetBuffer;
    SHADER_PUBLIC SHADER_PTR(PrimitiveCount) primitiveCountBuffer;
    SHADER_PUBLIC uint32_t instanceCount;
};

SHADER_PUBLIC struct PrefixSumPushConstant
{
    // Read-Write
    SHADER_PUBLIC SHADER_PTR(PrimitiveCount) primitiveCountBuffer;

    // Read-Only
    SHADER_PUBLIC uint32_t highestPrimitiveIndex;
};

SHADER_PUBLIC struct IndirectWritePushConstant
{
    // Read-Only
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(MeshletPrimitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;

    SHADER_PUBLIC SHADER_PTR(uint32_t) packedVisibilityBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) instanceOffsetBuffer;
    SHADER_PUBLIC SHADER_PTR(PrimitiveCount) primitiveCountBuffer;
    // Read-Write
    SHADER_PUBLIC SHADER_PTR(Instance) compactedInstanceBuffer;
    SHADER_PUBLIC SHADER_PTR(InstancedMeshIndirectCountBuffer) indirectCountBuffer;
    SHADER_PUBLIC SHADER_PTR(InstancedMeshIndirectDrawParameters) indirectBuffer;
};

SHADER_PUBLIC struct InstancedMeshShadingPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;

    SHADER_PUBLIC SHADER_PTR(Vertex) vertexBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletVerticesBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletTrianglesBuffer;
    SHADER_PUBLIC SHADER_PTR(Meshlet) meshletBuffer;

    SHADER_PUBLIC SHADER_PTR(InstancedMeshIndirectDrawParameters) indirectBuffer;

    SHADER_PUBLIC SHADER_PTR(Instance) compactedInstanceBuffer;
    SHADER_PUBLIC SHADER_PTR(MaterialProperties) materialBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
};

SHADER_PUBLIC struct DeferredResolvePushConstant {
    SHADER_PUBLIC float4 directionalLightDirection; // direction (xyz), intensity (w)
    SHADER_PUBLIC float4 directionalLightColor; // color (xyz), padding
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint2 extent;
    SHADER_PUBLIC uint32_t albedoIndex;
    SHADER_PUBLIC uint32_t normalIndex;
    SHADER_PUBLIC uint32_t pbrIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t velocityIndex;
    SHADER_PUBLIC uint32_t pointSamplerIndex;
    SHADER_PUBLIC uint32_t outputImageIndex;
};

SHADER_PUBLIC struct TemporalAntialiasingPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t pointSamplerIndex;
    SHADER_PUBLIC uint32_t linearSamplerIndex;
    SHADER_PUBLIC uint32_t colorResolvedIndex;
    SHADER_PUBLIC uint32_t colorHistoryIndex;
    SHADER_PUBLIC uint32_t velocityIndex;
    SHADER_PUBLIC uint32_t outputImageIndex;
};

#endif //WILL_ENGINE_PUSH_CONSTANT_INTEROP_H
