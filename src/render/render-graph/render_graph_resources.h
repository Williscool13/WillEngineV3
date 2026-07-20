//
// Created by William on 2025-12-27.
//

#ifndef WILL_ENGINE_RENDER_GRAPH_RESOURCES_H
#define WILL_ENGINE_RENDER_GRAPH_RESOURCES_H

#include <string>

#include <volk.h>
#include <vulkan/vk_enum_string_helper.h>

#include "core/containers/array.h"
#include "core/containers/inline_function.h"
#include "core/containers/inline_string.h"
#include "core/memory/handle.h"
#include "core/containers/inline_vector.h"
#include "core/memory/linear_allocator.h"
#include "render/render_config.h"
#include "render/interface/render_interface.h"
#include "render/vulkan/vk_resources.h"
#include "spdlog/spdlog.h"

namespace Render
{
struct TextureResource;
using TransientImageHandle = Core::Handle<TextureResource>;

struct BufferResource;
using TransientASHandle = Core::Handle<BufferResource>;

enum class RenderCategory : uint64_t
{
    Untagged = 0,
    Geometry            = 1ull << 0,
    WorldGridBinning    = 1ull << 1,
    FrustumBinning      = 1ull << 2,
    LightingResolve     = 1ull << 3,
    WorldCache          = 1ull << 4,
    DDGI                = 1ull << 5,
    GroundTruth         = 1ull << 6,
    FinalGather         = 1ull << 7,
    AmbientOcclusion    = 1ull << 8,
    DirectionalLighting = 1ull << 9,
    ReSTIRDI            = 1ull << 10,
    ReGIR               = 1ull << 11,
    ReLAX               = 1ull << 12,
    ReBLUR              = 1ull << 13,
    ReflectionsShade    = 1ull << 14,
    ReflectionsDenoise  = 1ull << 15,
    AntiAliasing        = 1ull << 16,
    PostProcessing      = 1ull << 17,
    Scene               = 1ull << 18,
    UI                  = 1ull << 19,
    Debug               = 1ull << 20,
    HeroDirectionalLight = 1ull << 21,
};

inline constexpr uint32_t RENDER_CATEGORY_BIT_COUNT = 22;
inline constexpr const char* RENDER_CATEGORY_NAMES[RENDER_CATEGORY_BIT_COUNT] = {
    "Geometry",
    "WorldGridBinning",
    "FrustumBinning",
    "LightingResolve",
    "WorldCache",
    "DDGI",
    "GroundTruth",
    "FinalGather",
    "AmbientOcclusion",
    "DirectionalLighting",
    "ReSTIRDI",
    "ReGIR",
    "ReLAX",
    "ReBLUR",
    "ReflectionsShade",
    "ReflectionsDenoise",
    "AntiAliasing",
    "PostProcessing",
    "Scene",
    "UI",
    "Debug",
    "HeroDirectionalLight",
};

inline RenderCategory operator|(RenderCategory a, RenderCategory b)
{
    return static_cast<RenderCategory>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

inline RenderCategory& operator|=(RenderCategory& a, RenderCategory b)
{
    a = a | b;
    return a;
}

inline bool HasResourceCategory(RenderCategory flags, RenderCategory check)
{
    return (static_cast<uint64_t>(flags) & static_cast<uint64_t>(check)) != 0;
}

enum class RenderCategoryGroup : uint8_t
{
    Untagged,
    Geometry,
    Lighting,
    ReSTIR,
    Reflections,
    PostProcessing,
    Scene,
    UI,
    Debug,
    Count,
};

inline constexpr uint32_t RENDER_CATEGORY_GROUP_COUNT = static_cast<uint32_t>(RenderCategoryGroup::Count);
inline constexpr const char* RENDER_CATEGORY_GROUP_NAMES[RENDER_CATEGORY_GROUP_COUNT] = {
    "Untagged",
    "Geometry",
    "Lighting",
    "ReSTIR",
    "Reflections",
    "PostProcessing",
    "Scene",
    "UI",
    "Debug",
};

// Indexed by leaf bit position (matches RenderCategory declaration order above).
inline constexpr RenderCategoryGroup RENDER_CATEGORY_GROUP_OF[RENDER_CATEGORY_BIT_COUNT] = {
    /*Geometry*/ RenderCategoryGroup::Geometry,
    /*WorldGridBinning*/ RenderCategoryGroup::Lighting,
    /*FrustumBinning*/ RenderCategoryGroup::Lighting,
    /*LightingResolve*/ RenderCategoryGroup::Lighting,
    /*WorldCache*/ RenderCategoryGroup::Lighting,
    /*DDGI*/ RenderCategoryGroup::Lighting,
    /*GroundTruth*/ RenderCategoryGroup::Lighting,
    /*FinalGather*/ RenderCategoryGroup::Lighting,
    /*AmbientOcclusion*/ RenderCategoryGroup::Lighting,
    /*DirectionalLighting*/ RenderCategoryGroup::Lighting,
    /*ReSTIRDI*/ RenderCategoryGroup::ReSTIR,
    /*ReGIR*/ RenderCategoryGroup::ReSTIR,
    /*ReLAX*/ RenderCategoryGroup::ReSTIR,
    /*ReBLUR*/ RenderCategoryGroup::ReSTIR,
    /*ReflectionsShade*/ RenderCategoryGroup::Reflections,
    /*ReflectionsDenoise*/ RenderCategoryGroup::Reflections,
    /*AntiAliasing*/ RenderCategoryGroup::PostProcessing,
    /*PostProcessing*/ RenderCategoryGroup::PostProcessing,
    /*Scene*/ RenderCategoryGroup::Scene,
    /*UI*/ RenderCategoryGroup::UI,
    /*Debug*/ RenderCategoryGroup::Debug,
    /*HeroDirectionalLight*/ RenderCategoryGroup::Lighting,
};

struct VRAMReport
{
    VkDeviceSize logical[RENDER_CATEGORY_BIT_COUNT]{};
    VkDeviceSize logicalTotal{0};

    VkDeviceSize physicalExclusive[RENDER_CATEGORY_BIT_COUNT]{};
    VkDeviceSize physicalSharedPoolBytes{0};
    VkDeviceSize physicalTotal{0};

    RenderCategory sharedPoolCategories{RenderCategory::Untagged};

    VkDeviceSize logicalGroup[RENDER_CATEGORY_GROUP_COUNT]{};
    VkDeviceSize physicalExclusiveGroup[RENDER_CATEGORY_GROUP_COUNT]{};
};

/** Per-frame GPU pass timing, bucketed by RenderCategory leaf and rolled up into RenderCategoryGroup, in milliseconds. */
struct GPUProfileSnapshot
{
    float leafMs[RENDER_CATEGORY_BIT_COUNT]{};
    float groupMs[RENDER_CATEGORY_GROUP_COUNT]{};
    float totalMs{0.0f};
};

enum class DepthAccessType
{
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
};

inline DepthAccessType operator|(DepthAccessType a, DepthAccessType b)
{
    return static_cast<DepthAccessType>(static_cast<int>(a) | static_cast<int>(b));
}

inline DepthAccessType& operator|=(DepthAccessType& a, DepthAccessType b)
{
    a = a | b;
    return a;
}

inline DepthAccessType operator&(DepthAccessType a, DepthAccessType b)
{
    return static_cast<DepthAccessType>(static_cast<int>(a) & static_cast<int>(b));
}

inline bool operator!(DepthAccessType a)
{
    return static_cast<int>(a) == 0;
}

enum class ImageChannelType
{
    Float4,
    Float2,
    Float,
    UInt4,
    UInt2,
    UInt
};

inline ImageChannelType GetImageChannelType(VkFormat format, VkImageAspectFlags aspect)
{
    if (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) {
        // Depth-Stencil, mip chain will be depth aspect. Stencil mips are not supported in this engine
        return ImageChannelType::Float;
    }

    if (aspect & VK_IMAGE_ASPECT_STENCIL_BIT) {
        return ImageChannelType::UInt;
    }
    switch (format) {
        case VK_FORMAT_R32G32B32A32_SFLOAT:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
            return ImageChannelType::Float4;
        case VK_FORMAT_R32G32_SFLOAT:
        case VK_FORMAT_R16G16_SFLOAT:
        case VK_FORMAT_R8G8_UNORM:
            return ImageChannelType::Float2;
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_R8_UNORM:
            return ImageChannelType::Float;
        case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R16G16B16A16_UINT:
        case VK_FORMAT_R8G8B8A8_UINT:
            return ImageChannelType::UInt4;
        case VK_FORMAT_R32G32_UINT:
        case VK_FORMAT_R16G16_UINT:
            return ImageChannelType::UInt2;
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R8_UINT:
            return ImageChannelType::UInt;
        default:
            SPDLOG_ERROR("Unsupported image channel format: {}", string_VkFormat(format));
            return ImageChannelType::Float4;
    }
}

struct PipelineEvent
{
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
};

struct ResourceDimensions
{
    enum class Type { Image, Buffer, AccelerationStructure } type = Type::Image;

    // Image fields
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t levels = 1;
    uint32_t layers = 1;
    uint32_t samples = 1;
    VkImageUsageFlags imageUsage = 0;

    // Buffer fields
    VkDeviceSize bufferSize = 0;
    VkBufferUsageFlags bufferUsage = 0;
    VkDeviceSize bufferMinAlignment = 0;

    // Shared
    StringID resourceId;

    [[nodiscard]] bool IsBuffer() const { return type == Type::Buffer || type == Type::AccelerationStructure; }
    [[nodiscard]] bool IsImage() const { return type == Type::Image; }
    // Basically a buffer that also additionally allocates an AS
    [[nodiscard]] bool IsAccelerationStructure() const { return type == Type::AccelerationStructure; }

    bool operator==(const ResourceDimensions& other) const
    {
        return bufferSize == other.bufferSize &&
               bufferUsage == other.bufferUsage &&
               bufferMinAlignment == other.bufferMinAlignment &&
               type == other.type &&
               format == other.format &&
               width == other.width &&
               height == other.height &&
               depth == other.depth &&
               levels == other.levels &&
               layers == other.layers &&
               samples == other.samples;
    }
};

struct PhysicalResource
{
    Core::InlineString<> debugName{};
    Core::InlineString<1024> usageChain{};

    RenderCategory category{RenderCategory::Untagged};

    ResourceDimensions dimensions;
    PipelineEvent event;
    bool bIsImported = false;
    bool bDisableBarriers = false;

    bool bCanAlias = true;
    bool bIsViewportScaled = false;
    bool bIsSwapchain = false;

    uint64_t lastUsedFrame = 0;

    Core::InlineVector<uint32_t, 16> logicalResourceIndices;

    // Image resources (valid if dimensions.is_image())
    VkImage image{VK_NULL_HANDLE};
    VkImageView imageView{VK_NULL_HANDLE};
    Core::Array<VkImageView, RDG_MAX_MIP_LEVELS> mipViews{};
    // Only for depth+stencil images
    VkImageView depthOnlyView{VK_NULL_HANDLE};
    VkImageView stencilOnlyView{VK_NULL_HANDLE};

    VmaAllocation imageAllocation{VK_NULL_HANDLE};
    VkImageAspectFlags aspect{VK_IMAGE_ASPECT_NONE};

    TransientImageHandle sampledDescriptorHandle{TransientImageHandle::INVALID};
    Core::Array<TransientImageHandle, RDG_MAX_MIP_LEVELS> storageMipDescriptorHandles{};
    TransientImageHandle depthOnlyDescriptorHandle{TransientImageHandle::INVALID};
    TransientImageHandle stencilOnlyDescriptorHandle{TransientImageHandle::INVALID};
    bool descriptorWritten{false};

    // Buffer resources (valid if dimensions.is_buffer())
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation bufferAllocation = VK_NULL_HANDLE;
    VkDeviceAddress bufferAddress = 0;
    bool addressRetrieved = false;

    VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
    TransientASHandle asDescriptorHandle{TransientASHandle::INVALID};

    [[nodiscard]] bool IsAllocated() const { return dimensions.IsImage() ? (image != VK_NULL_HANDLE) : (buffer != VK_NULL_HANDLE); }

    [[nodiscard]] bool NeedsDescriptorWrite() const { return dimensions.IsImage() && IsAllocated() && !descriptorWritten; }

    [[nodiscard]] bool NeedsAddressRetrieval() const
    {
        return dimensions.IsBuffer() && IsAllocated() && !addressRetrieved && (dimensions.bufferUsage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) == VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
};

struct TextureInfo
{
    VkFormat format{VK_FORMAT_UNDEFINED};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t mipLevels{1};
};

struct TextureResource
{
    StringID textureId;
    uint32_t index;
    uint32_t physicalIndex = UINT32_MAX;
    bool bCanUseAliasedTexture = true;

    RenderCategory category{RenderCategory::Untagged};

    std::optional<VkClearValue> clear{std::nullopt};
    bool bIsViewportScaled = false;

    TextureInfo textureInfo;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageUsageFlags accumulatedUsage;

    uint32_t firstPass = UINT32_MAX;
    uint32_t lastPass = 0;

    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    [[nodiscard]] bool HasPhysical() const { return physicalIndex != UINT32_MAX; }

    [[nodiscard]] bool HasFinalLayout() const { return finalLayout != VK_IMAGE_LAYOUT_UNDEFINED; }
};

struct BufferInfo
{
    VkDeviceSize size;
    VkBufferUsageFlags usage;
};

struct BufferResource
{
    StringID bufferId;
    uint32_t index = UINT32_MAX;
    uint32_t physicalIndex = UINT32_MAX;
    bool bCanUseAliasedBuffer = true;
    bool bIsViewportScaled = false;
    bool bIsAccelerationStructure = false;

    uint32_t carriedCount = 0;

    RenderCategory category{RenderCategory::Untagged};

    BufferInfo bufferInfo = {};
    VkBufferUsageFlags accumulatedUsage = 0;
    VkDeviceSize minAlignment = 0;

    uint32_t firstPass = UINT32_MAX;
    uint32_t lastPass = 0;

    [[nodiscard]] bool HasPhysical() const { return physicalIndex != UINT32_MAX; }
};

struct UploadAllocation
{
    void* ptr;
    VkDeviceAddress address;
    size_t offset;
};

struct TransientUploadArena
{
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation bufferAllocation{VK_NULL_HANDLE};
    void* mappedData{nullptr};
    VkDeviceAddress address{0};
    Core::LinearAllocator allocator{RDG_DEFAULT_UPLOAD_LINEAR_ALLOCATOR_SIZE};
    size_t size{RDG_DEFAULT_UPLOAD_LINEAR_ALLOCATOR_SIZE};
};

struct TransientReadback
{
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation bufferAllocation{VK_NULL_HANDLE};
    void* mappedData{nullptr};
};

struct TextureFrameCarryover
{
    StringID srcName;
    StringID dstName;

    VkImage physicalImage{};
    TextureInfo textInfo;
    VkImageLayout layout{};
    VkImageUsageFlags accumulatedUsage{};
};

struct BufferFrameCarryover
{
    StringID srcName;
    StringID dstName;
    uint32_t srcCarriedCount{0};

    VkBuffer buffer{};
    BufferInfo bufferInfo{};
    VkBufferUsageFlags accumulatedUsage{};
};

struct PersistentBuffer
{
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VkDeviceAddress address{0};
    VkDeviceSize capacity{0};
    uint64_t userData{0};
    uint64_t userData2{0};
    PipelineEvent lastState{};
};

struct PersistentBufferSlots
{
    StringID name{};
    VkBufferUsageFlags usage{};
    Core::Array<PersistentBuffer, Core::FRAME_BUFFER_COUNT> slots{};
    /** Called on each slot when the buffer is reallocated or destroyed. Use to clean up userData (e.g. VkAccelerationStructureKHR). */
    Core::InlineFunction<void(uint64_t userData), 32> onDestroyUserData{};
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_RESOURCES_H
