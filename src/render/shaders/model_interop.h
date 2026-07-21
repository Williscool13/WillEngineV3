//
// Created by William on 2025-12-15.
//


#ifndef WILLENGINEV3_MODEL_INTEROP_H
#define WILLENGINEV3_MODEL_INTEROP_H

#ifdef __SLANG__
import constants_interop;
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#else
#include <glm/glm.hpp>
#include <cstdint>
#include "constants_interop.h"

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
#endif // __SLANG__

SHADER_PUBLIC struct DebugVertex
{
    SHADER_PUBLIC float3 position;
    SHADER_PUBLIC float pad;
    SHADER_PUBLIC float4 color;
};

SHADER_PUBLIC struct DebugLineSegment
{
    SHADER_PUBLIC float3 a;
    SHADER_PUBLIC float width;
    SHADER_PUBLIC float3 b;
    SHADER_PUBLIC float pad;
    SHADER_PUBLIC float4 color;
};

SHADER_PUBLIC struct GPUDebugDrawArgs
{
    SHADER_PUBLIC uint32_t groupCountX;
    SHADER_PUBLIC uint32_t groupCountY;
    SHADER_PUBLIC uint32_t groupCountZ;
    SHADER_PUBLIC uint32_t segmentCount;
    SHADER_PUBLIC uint32_t capacity;
};

SHADER_PUBLIC struct GPUDebugSphereArgs
{
    SHADER_PUBLIC uint32_t vertexCount;
    SHADER_PUBLIC uint32_t instanceCount;
    SHADER_PUBLIC uint32_t firstVertex;
    SHADER_PUBLIC uint32_t firstInstance;
    SHADER_PUBLIC uint32_t capacity;
};

SHADER_PUBLIC struct DebugSphereInstance
{
    SHADER_PUBLIC float3 center;
    SHADER_PUBLIC float radius;
    SHADER_PUBLIC float4 shR; // L1 SH per channel, pre-scaled so shaded color = sh.x + dot(sh.yzw, normal)
    SHADER_PUBLIC float4 shG;
    SHADER_PUBLIC float4 shB;
};

SHADER_PUBLIC struct GPUDebugCubeArgs
{
    SHADER_PUBLIC uint32_t vertexCount;
    SHADER_PUBLIC uint32_t instanceCount;
    SHADER_PUBLIC uint32_t firstVertex;
    SHADER_PUBLIC uint32_t firstInstance;
    SHADER_PUBLIC uint32_t capacity;
};

SHADER_PUBLIC struct DebugCubeInstance
{
    SHADER_PUBLIC float3 center;
    SHADER_PUBLIC float halfExtent;
    SHADER_PUBLIC float4 color;
};


SHADER_PUBLIC struct VertexPosition
{
    SHADER_PUBLIC uint32_t pos0; // X, Y
    SHADER_PUBLIC uint32_t pos1; // Z, padding
};

SHADER_PUBLIC struct VertexAttribute
{
    SHADER_PUBLIC uint32_t normalOct; // snorm8: bits[7:0]=X, bits[15:8]=Y
    SHADER_PUBLIC uint32_t tangentOct; // snorm8: bits[7:0]=X, bits[15:8]=Y; bit[16]=sign(0=neg,1=pos)
    SHADER_PUBLIC uint32_t texcoord; // float16: bits[15:0]=U, bits[31:16]=V
    SHADER_PUBLIC uint32_t color; // unorm8: bits[7:0]=R, bits[15:8]=G, bits[23:16]=B, bits[31:24]=A
};

SHADER_PUBLIC struct Meshlet
{
    SHADER_PUBLIC float4 meshletBoundingSphere;

    SHADER_PUBLIC float3 coneApex;
    SHADER_PUBLIC float coneCutoff;

    SHADER_PUBLIC float3 coneAxis;
    SHADER_PUBLIC uint32_t vertexOffset;

    SHADER_PUBLIC uint32_t meshletVertexOffset;
    SHADER_PUBLIC uint32_t meshletTriangleOffset;
    SHADER_PUBLIC uint32_t meshletVertexCount;
    SHADER_PUBLIC uint32_t meshletTriangleCount;
};

SHADER_PUBLIC struct Primitive
{
    SHADER_PUBLIC int4 meshletOffset;
    SHADER_PUBLIC int4 meshletCount;
    SHADER_PUBLIC float4 boundingSphere; // {3} center, {1} radius
    SHADER_PUBLIC float3 boundingBoxMin;
    SHADER_PUBLIC uint32_t bHasTransparent;
    SHADER_PUBLIC float3 boundingBoxMax;
    SHADER_PUBLIC uint32_t indexOffset;
};

SHADER_PUBLIC struct MaterialProperties
{
    SHADER_PUBLIC uint32_t shadingBucketIndex;
    SHADER_PUBLIC uint32_t lightingBucketIndex;
    SHADER_PUBLIC uint32_t padding1;
    SHADER_PUBLIC uint32_t padding2;

    // Base PBR properties
    SHADER_PUBLIC float4 colorFactor;
    SHADER_PUBLIC float4 metalRoughFactors; // x: metallic, y: roughness, z: pad, w: pad

    // Texture indices
    SHADER_PUBLIC int4 textureImageIndices; // x: color, y: metallic-rough, z: normal, w: emissive
    SHADER_PUBLIC int4 textureSamplerIndices; // x: color, y: metallic-rough, z: normal, w: emissive
    SHADER_PUBLIC int4 textureImageIndices2; // x: occlusion, y: packed NRM, z: pad, w: pad
    SHADER_PUBLIC int4 textureSamplerIndices2; // x: occlusion, y: packed NRM, z: pad, w: pad

    // UV transform properties (scale.xy, offset.xy for each texture type)
    SHADER_PUBLIC float4 colorUvTransform; // xy: scale, zw: offset
    SHADER_PUBLIC float4 metalRoughUvTransform;
    SHADER_PUBLIC float4 normalUvTransform;
    SHADER_PUBLIC float4 emissiveUvTransform;
    SHADER_PUBLIC float4 occlusionUvTransform;

    // Additional material properties
    SHADER_PUBLIC float4 emissiveFactor; // xyz: emissive color, w: emissive strength
    SHADER_PUBLIC float4 alphaProperties; // x: alpha cutoff, y: alpha mode, z: double sided, w: unlit
    SHADER_PUBLIC float4 physicalProperties; // x: IOR, y: dispersion, z: normal scale, w: occlusion strength
};

SHADER_PUBLIC struct Instance
{
    SHADER_PUBLIC uint32_t primitiveIndex;
    SHADER_PUBLIC uint32_t modelIndex;
    SHADER_PUBLIC uint32_t materialIndex;
    SHADER_PUBLIC uint32_t lightingIndex;
    SHADER_PUBLIC uint64_t stableId;
    // Index into LightData.lights for an instance that is a light's representative emissive mesh
    SHADER_PUBLIC uint32_t lightIndex;
    // Base index into LightData.lights of this instance's emissive-triangle lights (+PrimitiveIndex())
    SHADER_PUBLIC uint32_t emissiveTriLightBase;
    SHADER_PUBLIC uint32_t flags;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
};

SHADER_PUBLIC struct Model
{
    SHADER_PUBLIC float4x4 modelMatrix;
    SHADER_PUBLIC float4x4 prevModelMatrix;
};

SHADER_PUBLIC struct ShadeDispatchParameters
{
    SHADER_PUBLIC uint32_t xDispatch;
    SHADER_PUBLIC uint32_t yDispatch;
    SHADER_PUBLIC uint32_t zDispatch;

    SHADER_PUBLIC uint32_t minX;
    SHADER_PUBLIC uint32_t maxX;
    SHADER_PUBLIC uint32_t minY;
    SHADER_PUBLIC uint32_t maxY;

    SHADER_PUBLIC uint32_t shadingIndex;
};

SHADER_PUBLIC struct LightingDispatchParameters
{
    SHADER_PUBLIC uint32_t xDispatch;
    SHADER_PUBLIC uint32_t yDispatch;
    SHADER_PUBLIC uint32_t zDispatch;

    SHADER_PUBLIC uint32_t minX;
    SHADER_PUBLIC uint32_t maxX;
    SHADER_PUBLIC uint32_t minY;
    SHADER_PUBLIC uint32_t maxY;

    SHADER_PUBLIC uint32_t lightingIndex;
};

#endif // WILLENGINEV3_MODEL_INTEROP_H
