//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_TYPES_H
#define WILL_ENGINE_MODEL_TYPES_H
#include <string>
#include <vector>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "offsetAllocator.hpp"
#include "render/descriptors/vk_bindless_resources_sampler_images.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
enum class MaterialType
{
    SOLID = 0,
    BLEND = 1,
    CUTOUT = 2,
};

struct PrimitiveProperty
{
    uint32_t index;
    int32_t materialIndex;
};

struct MeshInformation
{
    std::string name;
    std::vector<PrimitiveProperty> primitiveProperties;
};

struct Node
{
    std::string name{};
    uint32_t parent{~0u};
    uint32_t meshIndex{~0u};
    uint32_t depth{};

    uint32_t inverseBindIndex{~0u};

    glm::vec3 localTranslation{};
    glm::quat localRotation{};
    glm::vec3 localScale{};
};

struct AnimationSampler
{
    enum class Interpolation
    {
        Linear,
        Step,
        CubicSpline,
    };

    std::vector<float> timestamps;
    std::vector<float> values;
    Interpolation interpolation;
};

struct AnimationChannel
{
    enum class TargetPath
    {
        Translation,
        Rotation,
        Scale,
        Weights,
    };

    uint32_t samplerIndex;
    uint32_t targetNodeIndex;
    TargetPath targetPath;
};

struct Animation
{
    std::string name;
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;
    float duration;
};

struct ModelData
{
    bool bIsSkinned{};
    std::vector<MeshInformation> meshes{};
    std::vector<Animation> animations{};

    std::vector<glm::mat4> inverseBindMatrices{};

    std::vector<Sampler> samplers{};
    std::vector<AllocatedImage> images{};
    std::vector<ImageView> imageViews{};
    std::vector<MaterialProperties> materials{};

    std::vector<BindlessSamplerHandle> samplerIndexToDescriptorBufferIndexMap{};
    std::vector<BindlessTextureHandle> textureIndexToDescriptorBufferIndexMap{};

    OffsetAllocator::Allocation vertexAllocation{};
    OffsetAllocator::Allocation meshletVertexAllocation{};
    OffsetAllocator::Allocation meshletTriangleAllocation{};
    OffsetAllocator::Allocation meshletAllocation{};
    OffsetAllocator::Allocation primitiveAllocation{};

    ModelData() = default;

    ModelData(const ModelData&) = delete;

    ModelData& operator=(const ModelData&) = delete;

    ModelData(ModelData&&) noexcept = default;

    ModelData& operator=(ModelData&&) noexcept = default;

    void Reset()
    {
        meshes.clear();
        animations.clear();
        inverseBindMatrices.clear();
        samplers.clear();
        images.clear();
        imageViews.clear();
        samplerIndexToDescriptorBufferIndexMap.clear();
        textureIndexToDescriptorBufferIndexMap.clear();

        assert(vertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE && "Vertex allocation should be freed before reset");
        assert(meshletVertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE && "Meshlet vertex allocation should be freed before reset");
        assert(meshletTriangleAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE && "Meshlet triangle allocation should be freed before reset");
        assert(meshletAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE && "Meshlet allocation should be freed before reset");
        assert(primitiveAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE && "Primitive allocation should be freed before reset");

        vertexAllocation = {};
        meshletVertexAllocation = {};
        meshletTriangleAllocation = {};
        meshletAllocation = {};
        primitiveAllocation = {};
    }
};

} // Render

#endif //WILL_ENGINE_MODEL_TYPES_H
