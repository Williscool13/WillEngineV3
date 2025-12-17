//
// Created by William on 2025-12-15.
//


#ifndef WILLENGINEV3_MODEL_INTEROP_H
#define WILLENGINEV3_MODEL_INTEROP_H

#ifdef __SLANG__
module model_interop;
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#else
#include <glm/glm.hpp>
#include <cstdint>

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


SHADER_PUBLIC struct Vertex
{
    SHADER_PUBLIC float3 position;
    SHADER_PUBLIC float texcoordX;
    SHADER_PUBLIC float3 normal;
    SHADER_PUBLIC float texcoordV;
    SHADER_PUBLIC float4 tangent;
    SHADER_PUBLIC float4 color;
};

SHADER_PUBLIC struct SkinnedVertex
{
    SHADER_PUBLIC float3 position;
    SHADER_PUBLIC float texcoordU;
    SHADER_PUBLIC float3 normal;
    SHADER_PUBLIC float texcoordV;
    SHADER_PUBLIC float4 tangent;
    SHADER_PUBLIC float4 color;
    SHADER_PUBLIC uint4 joints;
    SHADER_PUBLIC float4 weights;
};


SHADER_PUBLIC struct Meshlet
{
    SHADER_PUBLIC float4 meshletBoundingSphere;

    SHADER_PUBLIC float3 coneApex;
    SHADER_PUBLIC float coneCutoff;

    SHADER_PUBLIC float3 coneAxis;
    SHADER_PUBLIC uint32_t vertexOffset;

    SHADER_PUBLIC uint32_t meshletVerticesOffset;
    SHADER_PUBLIC uint32_t meshletTriangleOffset;
    SHADER_PUBLIC uint32_t meshletVerticesCount;
    SHADER_PUBLIC uint32_t meshletTriangleCount;
};

SHADER_PUBLIC struct MeshletPrimitive
{
    SHADER_PUBLIC uint32_t meshletOffset;
    SHADER_PUBLIC uint32_t meshletCount;
    SHADER_PUBLIC uint32_t materialIndex;
    SHADER_PUBLIC uint32_t bHasTransparent;
    SHADER_PUBLIC float4 boundingSphere; // {3} center, {1} radius
};

SHADER_PUBLIC struct MaterialProperties
{
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

#endif // WILLENGINEV3_MODEL_INTEROP_H