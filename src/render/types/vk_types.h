//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_VK_TYPES_H
#define WILL_ENGINE_VK_TYPES_H

#include <glm/glm.hpp>

namespace Render
{
struct Vertex
{
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 color{1.0f};
    glm::vec2 uv{0, 0};
};

struct SkinnedVertex
{
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 color{1.0f};
    glm::vec2 uv{0, 0};
    glm::uvec4 joints;
    glm::vec4 weights;
};

struct MaterialProperties
{
    // Base PBR properties
    glm::vec4 colorFactor{1.0f};
    glm::vec4 metalRoughFactors{0.0f, 1.0f, 0.0f, 0.0f}; // x: metallic, y: roughness, z: pad, w: pad

    // Texture indices
    glm::ivec4 textureImageIndices{-1}; // x: color, y: metallic-rough, z: normal, w: emissive
    glm::ivec4 textureSamplerIndices{-1}; // x: color, y: metallic-rough, z: normal, w: emissive
    glm::ivec4 textureImageIndices2{-1}; // x: occlusion, y: packed NRM, z: pad, w: pad
    glm::ivec4 textureSamplerIndices2{-1}; // x: occlusion, y: packed NRM, z: pad, w: pad

    // UV transform properties (scale.xy, offset.xy for each texture type)
    glm::vec4 colorUvTransform{1.0f, 1.0f, 0.0f, 0.0f}; // xy: scale, zw: offset
    glm::vec4 metalRoughUvTransform{1.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 normalUvTransform{1.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 emissiveUvTransform{1.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 occlusionUvTransform{1.0f, 1.0f, 0.0f, 0.0f};

    // Additional material properties
    glm::vec4 emissiveFactor{0.0f, 0.0f, 0.0f, 1.0f}; // xyz: emissive color, w: emissive strength
    glm::vec4 alphaProperties{0.5f, 0.0f, 0.0f, 0.0f}; // x: alpha cutoff, y: alpha mode, z: double sided, w: unlit
    glm::vec4 physicalProperties{1.5f, 0.0f, 1.0f, 0.0f}; // x: IOR, y: dispersion, z: normal scale, w: occlusion strength
};

struct UIVertex
{
    glm::vec2 position{0, 0};
    glm::vec2 uv{0, 0};
    uint32_t color{0xFFFFFFFF};
    uint32_t samplerIndex{0};
    uint32_t textureIndex{0};
    uint32_t bIsText{1};
};
} // Render

#endif //WILL_ENGINE_VK_TYPES_H
