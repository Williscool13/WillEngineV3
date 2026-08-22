//
// Created by William on 2026-08-01.
//

#include "render/passes/nrd_denoiser.h"

#include <tracy/Tracy.hpp>

#include <glm/glm.hpp>

#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "core/memory/tlsf_allocator.h"
#include "engine/logging/engine_log.h"
#include "render/render_config.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_graph.h"
#include "render/render-graph/render_pass.h"
#include "render/resource_manager.h"
#include "render/shaders/push_constant_interop.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"

namespace Render
{
namespace
{
const StringID NRD_IN_MV_SID = SID("nrd_in_mv");
const StringID NRD_IN_NORMAL_ROUGHNESS_SID = SID("nrd_in_normal_roughness");
const StringID NRD_IN_VIEWZ_SID = SID("nrd_in_viewz");
const StringID NRD_IN_DIFF_SID = SID("nrd_in_diff_radiance_hitdist");
const StringID NRD_IN_SPEC_SID = SID("nrd_in_spec_radiance_hitdist");
const StringID NRD_OUT_DIFF_SID = SID("nrd_out_diff_radiance_hitdist");
const StringID NRD_OUT_SPEC_SID = SID("nrd_out_spec_radiance_hitdist");

constexpr VkFormat NRD_IO_FORMATS[] = {
    VK_FORMAT_R16G16B16A16_SFLOAT, // IN_MV
    VK_FORMAT_A2B10G10R10_UNORM_PACK32, // IN_NORMAL_ROUGHNESS
    VK_FORMAT_R32_SFLOAT, // IN_VIEWZ
    VK_FORMAT_R16G16B16A16_SFLOAT, // IN_DIFF
    VK_FORMAT_R16G16B16A16_SFLOAT, // IN_SPEC
    VK_FORMAT_R16G16B16A16_SFLOAT, // OUT_DIFF
    VK_FORMAT_R16G16B16A16_SFLOAT, // OUT_SPEC
};

constexpr const char* NRD_IO_NAMES[] = {
    "nrd_in_mv",
    "nrd_in_normal_roughness",
    "nrd_in_viewz",
    "nrd_in_diff_radiance_hitdist",
    "nrd_in_spec_radiance_hitdist",
    "nrd_out_diff_radiance_hitdist",
    "nrd_out_spec_radiance_hitdist",
};

const StringID NRD_IO_SIDS[] = {
    NRD_IN_MV_SID,
    NRD_IN_NORMAL_ROUGHNESS_SID,
    NRD_IN_VIEWZ_SID,
    NRD_IN_DIFF_SID,
    NRD_IN_SPEC_SID,
    NRD_OUT_DIFF_SID,
    NRD_OUT_SPEC_SID,
};

VkFormat GetVkFormat(nrd::Format format)
{
    switch (format) {
        case nrd::Format::R8_UNORM: return VK_FORMAT_R8_UNORM;
        case nrd::Format::R8_SNORM: return VK_FORMAT_R8_SNORM;
        case nrd::Format::R8_UINT: return VK_FORMAT_R8_UINT;
        case nrd::Format::R8_SINT: return VK_FORMAT_R8_SINT;
        case nrd::Format::RG8_UNORM: return VK_FORMAT_R8G8_UNORM;
        case nrd::Format::RG8_SNORM: return VK_FORMAT_R8G8_SNORM;
        case nrd::Format::RG8_UINT: return VK_FORMAT_R8G8_UINT;
        case nrd::Format::RG8_SINT: return VK_FORMAT_R8G8_SINT;
        case nrd::Format::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case nrd::Format::RGBA8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
        case nrd::Format::RGBA8_UINT: return VK_FORMAT_R8G8B8A8_UINT;
        case nrd::Format::RGBA8_SINT: return VK_FORMAT_R8G8B8A8_SINT;
        case nrd::Format::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case nrd::Format::R16_UNORM: return VK_FORMAT_R16_UNORM;
        case nrd::Format::R16_SNORM: return VK_FORMAT_R16_SNORM;
        case nrd::Format::R16_UINT: return VK_FORMAT_R16_UINT;
        case nrd::Format::R16_SINT: return VK_FORMAT_R16_SINT;
        case nrd::Format::R16_SFLOAT: return VK_FORMAT_R16_SFLOAT;
        case nrd::Format::RG16_UNORM: return VK_FORMAT_R16G16_UNORM;
        case nrd::Format::RG16_SNORM: return VK_FORMAT_R16G16_SNORM;
        case nrd::Format::RG16_UINT: return VK_FORMAT_R16G16_UINT;
        case nrd::Format::RG16_SINT: return VK_FORMAT_R16G16_SINT;
        case nrd::Format::RG16_SFLOAT: return VK_FORMAT_R16G16_SFLOAT;
        case nrd::Format::RGBA16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
        case nrd::Format::RGBA16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
        case nrd::Format::RGBA16_UINT: return VK_FORMAT_R16G16B16A16_UINT;
        case nrd::Format::RGBA16_SINT: return VK_FORMAT_R16G16B16A16_SINT;
        case nrd::Format::RGBA16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case nrd::Format::R32_UINT: return VK_FORMAT_R32_UINT;
        case nrd::Format::R32_SINT: return VK_FORMAT_R32_SINT;
        case nrd::Format::R32_SFLOAT: return VK_FORMAT_R32_SFLOAT;
        case nrd::Format::RG32_UINT: return VK_FORMAT_R32G32_UINT;
        case nrd::Format::RG32_SINT: return VK_FORMAT_R32G32_SINT;
        case nrd::Format::RG32_SFLOAT: return VK_FORMAT_R32G32_SFLOAT;
        case nrd::Format::RGB32_UINT: return VK_FORMAT_R32G32B32_UINT;
        case nrd::Format::RGB32_SINT: return VK_FORMAT_R32G32B32_SINT;
        case nrd::Format::RGB32_SFLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
        case nrd::Format::RGBA32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
        case nrd::Format::RGBA32_SINT: return VK_FORMAT_R32G32B32A32_SINT;
        case nrd::Format::RGBA32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case nrd::Format::R10_G10_B10_A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case nrd::Format::R10_G10_B10_A2_UINT: return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        case nrd::Format::R11_G11_B10_UFLOAT: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case nrd::Format::R9_G9_B9_E5_UFLOAT: return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
        default: return VK_FORMAT_UNDEFINED;
    }
}

uint32_t DivideUp(uint32_t x, uint32_t divisor)
{
    return divisor > 0 ? (x + divisor - 1) / divisor : x;
}

void SetImageDebugName(const VulkanContext* context, VkImage image, const char* name)
{
#ifdef WDEBUG
    VkDebugUtilsObjectNameInfoEXT nameInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
    nameInfo.objectHandle = reinterpret_cast<uint64_t>(image);
    nameInfo.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(context->device, &nameInfo);
#endif
}
} // namespace

NrdDenoiser::NrdDenoiser(VulkanContext* context, Core::TlsfAllocator& renderAlloc)
    : context(context),
      alloc(&renderAlloc),
      pipelines(&renderAlloc, Core::AllocTag::Render),
      samplers(&renderAlloc, Core::AllocTag::Render),
      poolTextures(&renderAlloc, Core::AllocTag::Render),
      retiredImages(&renderAlloc, Core::AllocTag::Render)
{
}

NrdDenoiser::~NrdDenoiser()
{
    ReleaseRetired(0, true);
    for (TrackedTexture& tex : poolTextures) {
        if (tex.view != VK_NULL_HANDLE) { vkDestroyImageView(context->device, tex.view, context->HostAllocCallbacks()); }
        if (tex.image != VK_NULL_HANDLE) { vmaDestroyImage(context->allocator, tex.image, tex.allocation); }
    }
    for (TrackedTexture& tex : ioTextures) {
        if (tex.view != VK_NULL_HANDLE) { vkDestroyImageView(context->device, tex.view, context->HostAllocCallbacks()); }
        if (tex.image != VK_NULL_HANDLE) { vmaDestroyImage(context->allocator, tex.image, tex.allocation); }
    }
    for (PipelineObjects& p : pipelines) {
        if (p.pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(context->device, p.pipeline, context->HostAllocCallbacks()); }
        if (p.layout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(context->device, p.layout, context->HostAllocCallbacks()); }
        if (p.setLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(context->device, p.setLayout, context->HostAllocCallbacks()); }
    }
    for (VkDescriptorPool pool : frameDescriptorPools) {
        if (pool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(context->device, pool, context->HostAllocCallbacks()); }
    }
    if (sharedDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(context->device, sharedDescriptorPool, context->HostAllocCallbacks()); }
    if (sharedSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(context->device, sharedSetLayout, context->HostAllocCallbacks()); }
    for (VkSampler sampler : samplers) {
        if (sampler != VK_NULL_HANDLE) { vkDestroySampler(context->device, sampler, context->HostAllocCallbacks()); }
    }
    if (instance != nullptr) {
        nrd::DestroyInstance(*instance);
        instance = nullptr;
    }
}

bool NrdDenoiser::EnsureInstance()
{
    if (bInitFailed) { return false; }
    if (bInitialized) { return true; }

    const nrd::DenoiserDesc denoiserDescs[] = {
        {NRD_RELAX_IDENTIFIER, nrd::Denoiser::RELAX_DIFFUSE_SPECULAR},
        {NRD_REBLUR_IDENTIFIER, nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR},
    };
    nrd::InstanceCreationDesc createDesc{};
    createDesc.denoisers = denoiserDescs;
    createDesc.denoisersNum = 2;

    if (nrd::CreateInstance(createDesc, instance) != nrd::Result::SUCCESS) {
        LOG_ERROR(Renderer, "[NRD] CreateInstance failed");
        bInitFailed = true;
        return false;
    }

    const nrd::LibraryDesc& lib = *nrd::GetLibraryDesc();
    const nrd::InstanceDesc& desc = *nrd::GetInstanceDesc(*instance);

    if (lib.normalEncoding != nrd::NormalEncoding::R10_G10_B10_A2_UNORM || lib.roughnessEncoding != nrd::RoughnessEncoding::LINEAR) {
        LOG_ERROR(Renderer, "[NRD] Library built with unexpected normal/roughness encoding ({}, {})", static_cast<uint32_t>(lib.normalEncoding), static_cast<uint32_t>(lib.roughnessEncoding));
        bInitFailed = true;
        return false;
    }

    // Samplers
    for (uint32_t i = 0; i < desc.samplersNum; i++) {
        const bool bNearest = desc.samplers[i] == nrd::Sampler::NEAREST_CLAMP;
        VkSamplerCreateInfo samplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = bNearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR,
            .minFilter = bNearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod = 16.0f,
        };
        VkSampler sampler = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSampler(context->device, &samplerInfo, context->HostAllocCallbacks(), &sampler));
        samplers.PushBack(sampler);
    }

    // Constant ring buffer, per frame-in-flight regions, dynamic offsets per dispatch
    const VkDeviceSize uboAlignment = VulkanContext::deviceInfo.properties.properties.limits.minUniformBufferOffsetAlignment;
    constantStride = static_cast<uint32_t>((glm::max<VkDeviceSize>(desc.constantBufferMaxDataSize, 4u) + uboAlignment - 1) & ~(uboAlignment - 1));
    constantSlotsPerFrame = glm::max(desc.descriptorPoolDesc.setsMaxNum, 1u);
    const VkDeviceSize ringSize = static_cast<VkDeviceSize>(constantStride) * constantSlotsPerFrame * Core::FRAME_BUFFER_COUNT;
    constantRing = AllocatedBuffer::CreateAllocatedStagingBuffer(context, ringSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    constantRing.SetDebugName("nrd_constant_ring");

    // Shared set (space 1): immutable samplers + dynamic constant buffer
    {
        Core::InlineVector<VkDescriptorSetLayoutBinding, 8> bindings{};
        for (uint32_t i = 0; i < desc.samplersNum; i++) {
            bindings.PushBack(VkDescriptorSetLayoutBinding{
                .binding = lib.spirvBindingOffsets.samplerOffset + desc.samplersBaseRegisterIndex + i,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .pImmutableSamplers = &samplers[i],
            });
        }
        bindings.PushBack(VkDescriptorSetLayoutBinding{
            .binding = lib.spirvBindingOffsets.constantBufferOffset + desc.constantBufferRegisterIndex,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        });

        VkDescriptorSetLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(bindings.Size()),
            .pBindings = bindings.Data(),
        };
        VK_CHECK(vkCreateDescriptorSetLayout(context->device, &layoutInfo, context->HostAllocCallbacks(), &sharedSetLayout));

        Core::Array<VkDescriptorPoolSize, 2> poolSizes{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, glm::max(desc.samplersNum, 1u)},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1},
        };
        VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.Size()),
            .pPoolSizes = poolSizes.Data(),
        };
        VK_CHECK(vkCreateDescriptorPool(context->device, &poolInfo, context->HostAllocCallbacks(), &sharedDescriptorPool));

        VkDescriptorSetAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = sharedDescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &sharedSetLayout,
        };
        VK_CHECK(vkAllocateDescriptorSets(context->device, &allocInfo, &sharedSet));

        VkDescriptorBufferInfo bufferInfo{constantRing.handle, 0, glm::max<VkDeviceSize>(desc.constantBufferMaxDataSize, 4u)};
        VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = sharedSet,
            .dstBinding = lib.spirvBindingOffsets.constantBufferOffset + desc.constantBufferRegisterIndex,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo = &bufferInfo,
        };
        vkUpdateDescriptorSets(context->device, 1, &write, 0, nullptr);
    }

    // Per-frame descriptor pools sized from NRD's own limits
    {
        Core::Array<VkDescriptorPoolSize, 2> poolSizes{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, glm::max(desc.descriptorPoolDesc.totalTexturesNum, 1u)},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, glm::max(desc.descriptorPoolDesc.totalStorageTexturesNum, 1u)},
        };
        VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = glm::max(desc.descriptorPoolDesc.setsMaxNum, 1u),
            .poolSizeCount = static_cast<uint32_t>(poolSizes.Size()),
            .pPoolSizes = poolSizes.Data(),
        };
        for (uint32_t i = 0; i < Core::FRAME_BUFFER_COUNT; i++) {
            VK_CHECK(vkCreateDescriptorPool(context->device, &poolInfo, context->HostAllocCallbacks(), &frameDescriptorPools[i]));
        }
    }

    // Per-pipeline set layout (space 0) from NRD resource ranges, then layout + pipeline from the embedded SPIRV blob
    for (uint32_t i = 0; i < desc.pipelinesNum; i++) {
        const nrd::PipelineDesc& pipelineDesc = desc.pipelines[i];
        PipelineObjects objects{};

        Core::InlineVector<VkDescriptorSetLayoutBinding, 64> bindings{};
        for (uint32_t r = 0; r < pipelineDesc.resourceRangesNum; r++) {
            const nrd::ResourceRangeDesc& range = pipelineDesc.resourceRanges[r];
            const bool bStorage = range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;
            const uint32_t baseRegister = (bStorage ? lib.spirvBindingOffsets.storageTextureAndBufferOffset : lib.spirvBindingOffsets.textureOffset) + desc.resourcesBaseRegisterIndex;
            for (uint32_t j = 0; j < range.descriptorsNum; j++) {
                bindings.PushBack(VkDescriptorSetLayoutBinding{
                    .binding = baseRegister + j,
                    .descriptorType = bStorage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                });
            }
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(bindings.Size()),
            .pBindings = bindings.Data(),
        };
        VK_CHECK(vkCreateDescriptorSetLayout(context->device, &layoutInfo, context->HostAllocCallbacks(), &objects.setLayout));

        const Core::Array<VkDescriptorSetLayout, 2> setLayouts{objects.setLayout, sharedSetLayout};
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<uint32_t>(setLayouts.Size()),
            .pSetLayouts = setLayouts.Data(),
        };
        VK_CHECK(vkCreatePipelineLayout(context->device, &pipelineLayoutInfo, context->HostAllocCallbacks(), &objects.layout));

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (!VkHelpers::LoadShaderModuleFromBlob(pipelineDesc.computeShaderSPIRV.bytecode, pipelineDesc.computeShaderSPIRV.size, context->device, context->HostAllocCallbacks(), &shaderModule)) {
            LOG_ERROR(Renderer, "[NRD] Failed to create shader module for pipeline {} ({})", i, pipelineDesc.shaderIdentifier);
            vkDestroyPipelineLayout(context->device, objects.layout, context->HostAllocCallbacks());
            vkDestroyDescriptorSetLayout(context->device, objects.setLayout, context->HostAllocCallbacks());
            bInitFailed = true;
            return false;
        }

        const VkPipelineShaderStageCreateInfo stage = VkHelpers::PipelineShaderStageCreateInfo(shaderModule, VK_SHADER_STAGE_COMPUTE_BIT, desc.shaderEntryPoint);
        VkComputePipelineCreateInfo pipelineInfo = VkHelpers::ComputePipelineCreateInfo(objects.layout, stage);
        // Remove descriptor buffer flag
        pipelineInfo.flags = 0;
        const VkResult pipelineResult = vkCreateComputePipelines(context->device, VK_NULL_HANDLE, 1, &pipelineInfo, context->HostAllocCallbacks(), &objects.pipeline);
        vkDestroyShaderModule(context->device, shaderModule, context->HostAllocCallbacks());
        if (pipelineResult != VK_SUCCESS) {
            LOG_ERROR(Renderer, "[NRD] Failed to create pipeline {} ({})", i, pipelineDesc.shaderIdentifier);
            vkDestroyPipelineLayout(context->device, objects.layout, context->HostAllocCallbacks());
            vkDestroyDescriptorSetLayout(context->device, objects.setLayout, context->HostAllocCallbacks());
            bInitFailed = true;
            return false;
        }

        pipelines.PushBack(objects);
    }

    LOG_INFO(Renderer, "[NRD] Initialized v{}.{}.{}: {} pipelines, {} permanent + {} transient pool textures, CB stride {} x {} slots",
             lib.versionMajor, lib.versionMinor, lib.versionBuild, desc.pipelinesNum, desc.permanentPoolSize, desc.transientPoolSize, constantStride, constantSlotsPerFrame);

    bInitialized = true;
    return true;
}

bool NrdDenoiser::CreateTrackedTexture(TrackedTexture& tex, VkFormat format, uint32_t width, uint32_t height, const char* debugName)
{
    VkImageCreateInfo imageInfo = VkHelpers::ImageCreateInfo(format, VkExtent3D{width, height, 1}, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    constexpr VmaAllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_AUTO};
    if (vmaCreateImage(context->allocator, &imageInfo, &allocInfo, &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
        LOG_ERROR(Renderer, "[NRD] Failed to create texture {} ({}x{})", debugName, width, height);
        return false;
    }
    VkImageViewCreateInfo viewInfo = VkHelpers::ImageViewCreateInfo(tex.image, format, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(context->device, &viewInfo, context->HostAllocCallbacks(), &tex.view));
    tex.format = format;
    tex.width = width;
    tex.height = height;
    tex.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    tex.access = VK_ACCESS_2_NONE;
    SetImageDebugName(context, tex.image, debugName);
    return true;
}

void NrdDenoiser::RetireTrackedTexture(TrackedTexture& tex, uint64_t frameNumber)
{
    if (tex.image != VK_NULL_HANDLE) {
        retiredImages.PushBack(RetiredImage{tex.image, tex.allocation, tex.view, frameNumber});
    }
    tex = TrackedTexture{};
}

void NrdDenoiser::ReleaseRetired(uint64_t frameNumber, bool bForce)
{
    for (size_t i = retiredImages.Size(); i > 0; i--) {
        RetiredImage& retired = retiredImages[i - 1];
        if (bForce || frameNumber > retired.retiredFrame + Core::FRAME_BUFFER_COUNT) {
            if (retired.view != VK_NULL_HANDLE) { vkDestroyImageView(context->device, retired.view, context->HostAllocCallbacks()); }
            vmaDestroyImage(context->allocator, retired.image, retired.allocation);
            retired = retiredImages[retiredImages.Size() - 1];
            retiredImages.PopBack();
        }
    }
}

void NrdDenoiser::EnsureResources(Core::Array<uint32_t, 2> renderExtent, uint64_t frameNumber)
{
    ZoneScoped;
    if (currentWidth == renderExtent[0] && currentHeight == renderExtent[1] && !poolTextures.IsEmpty()) {
        return;
    }

    for (TrackedTexture& tex : poolTextures) { RetireTrackedTexture(tex, frameNumber); }
    poolTextures.Clear();
    for (TrackedTexture& tex : ioTextures) { RetireTrackedTexture(tex, frameNumber); }

    const nrd::InstanceDesc& desc = *nrd::GetInstanceDesc(*instance);
    const uint32_t poolSize = desc.permanentPoolSize + desc.transientPoolSize;
    for (uint32_t i = 0; i < poolSize; i++) {
        const bool bPermanent = i < desc.permanentPoolSize;
        const nrd::TextureDesc& textureDesc = bPermanent ? desc.permanentPool[i] : desc.transientPool[i - desc.permanentPoolSize];
        const uint32_t w = glm::max(DivideUp(renderExtent[0], textureDesc.downsampleFactor), 1u);
        const uint32_t h = glm::max(DivideUp(renderExtent[1], textureDesc.downsampleFactor), 1u);
        const Core::InlineString<32> name = Core::InlineString<32>::Format(bPermanent ? "nrd_perm_%u" : "nrd_trans_%u", bPermanent ? i : i - desc.permanentPoolSize);
        TrackedTexture tex{};
        if (!CreateTrackedTexture(tex, GetVkFormat(textureDesc.format), w, h, name.c_str())) {
            bInitFailed = true;
            return;
        }
        poolTextures.PushBack(tex);
    }

    for (uint32_t i = 0; i < IO_COUNT; i++) {
        if (!CreateTrackedTexture(ioTextures[i], NRD_IO_FORMATS[i], renderExtent[0], renderExtent[1], NRD_IO_NAMES[i])) {
            bInitFailed = true;
            return;
        }
    }

    currentWidth = renderExtent[0];
    currentHeight = renderExtent[1];
    prevRectWidth = renderExtent[0];
    prevRectHeight = renderExtent[1];
    bPendingHistoryClear = true;
}

void NrdDenoiser::StageSettings(const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const Core::RELAXParams& relaxParams, const Core::ReBLURParams& reblurParams, uint64_t frameNumber, float renderFps, bool bHistoryReset)
{
    ZoneScoped;
    const Core::RELAXParams& params = relaxParams;
    const glm::mat4& proj = viewFamily.mainView.currentViewData.proj;
    const glm::mat4& prevProj = viewFamily.mainView.previousViewData.proj;

    // The gbuffer is OGL window-origin (row 0 = NDC y -1) while NRD reconstructs and projects D3D-style; negating the view's y row flips gFrustumUp and the clip-space y of worldToClip together.
    glm::mat4 view = viewFamily.mainView.currentViewData.view;
    glm::mat4 prevView = viewFamily.mainView.previousViewData.view;
    for (int c = 0; c < 4; c++) {
        view[c][1] = -view[c][1];
        prevView[c][1] = -prevView[c][1];
    }

    stagedCommon = nrd::CommonSettings{};
    memcpy(stagedCommon.viewToClipMatrix, &proj, sizeof(float) * 16);
    memcpy(stagedCommon.viewToClipMatrixPrev, &prevProj, sizeof(float) * 16);
    memcpy(stagedCommon.worldToViewMatrix, &view, sizeof(float) * 16);
    memcpy(stagedCommon.worldToViewMatrixPrev, &prevView, sizeof(float) * 16);

    // 2D screen-space motion in NDC units (gbuffer convention) + view-depth delta in .z
    stagedCommon.motionVectorScale[0] = 0.5f;
    stagedCommon.motionVectorScale[1] = 0.5f;
    stagedCommon.motionVectorScale[2] = 1.0f;
    stagedCommon.isMotionVectorInWorldSpace = false;

    // Same sample sequence and indexing as GenerateSceneData/ComputeRelaxJitterDelta, in pixel units
    const Core::AntiAliasingMode aaMode = viewFamily.aaConfig.mode;
    if (aaMode == Core::AntiAliasingMode::TAA || aaMode == Core::AntiAliasingMode::NaiveTAA || aaMode == Core::AntiAliasingMode::DonutTAA) {
        const HaltonSample& curr = HALTON_SEQUENCE[(frameNumber + 1) % HALTON_SEQUENCE_COUNT];
        const HaltonSample& prev = HALTON_SEQUENCE[frameNumber % HALTON_SEQUENCE_COUNT];
        stagedCommon.cameraJitter[0] = curr.x;
        stagedCommon.cameraJitter[1] = curr.y;
        stagedCommon.cameraJitterPrev[0] = prev.x;
        stagedCommon.cameraJitterPrev[1] = prev.y;
    }
    else if (aaMode == Core::AntiAliasingMode::SMAAT2X) {
        constexpr float SMAA_T2X_OFFSETS[2][2] = {{-0.25f, -0.25f}, {0.25f, 0.25f}};
        stagedCommon.cameraJitter[0] = SMAA_T2X_OFFSETS[frameNumber % 2][0];
        stagedCommon.cameraJitter[1] = SMAA_T2X_OFFSETS[frameNumber % 2][1];
        stagedCommon.cameraJitterPrev[0] = SMAA_T2X_OFFSETS[(frameNumber + 1) % 2][0];
        stagedCommon.cameraJitterPrev[1] = SMAA_T2X_OFFSETS[(frameNumber + 1) % 2][1];
    }

    stagedCommon.resourceSize[0] = static_cast<uint16_t>(renderExtent[0]);
    stagedCommon.resourceSize[1] = static_cast<uint16_t>(renderExtent[1]);
    stagedCommon.resourceSizePrev[0] = static_cast<uint16_t>(prevRectWidth);
    stagedCommon.resourceSizePrev[1] = static_cast<uint16_t>(prevRectHeight);
    stagedCommon.rectSize[0] = static_cast<uint16_t>(renderExtent[0]);
    stagedCommon.rectSize[1] = static_cast<uint16_t>(renderExtent[1]);
    stagedCommon.rectSizePrev[0] = static_cast<uint16_t>(prevRectWidth);
    stagedCommon.rectSizePrev[1] = static_cast<uint16_t>(prevRectHeight);

    // NRD's internal 16.66/timeDelta framerate scale then matches the engine's fps/60 clamp
    stagedCommon.timeDeltaBetweenFrames = renderFps > 0.0f ? 1000.0f / renderFps : 0.0f;
    stagedCommon.denoisingRange = activeBackend == NrdBackend::Reblur ? reblurParams.denoisingRange : params.denoisingRange;
    stagedCommon.disocclusionThreshold = activeBackend == NrdBackend::Reblur ? reblurParams.disocclusionThreshold : params.disocclusionThreshold;
    stagedCommon.frameIndex = static_cast<uint32_t>(frameNumber);
    stagedCommon.accumulationMode = bHistoryReset ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;

    stagedRelax = nrd::RelaxSettings{};
    stagedRelax.antilagSettings.accelerationAmount = params.historyAccelerationAmount;
    stagedRelax.antilagSettings.spatialSigmaScale = params.historyResetSpatialSigmaScale;
    stagedRelax.antilagSettings.temporalSigmaScale = params.historyResetTemporalSigmaScale;
    stagedRelax.antilagSettings.resetAmount = params.historyResetAmount;
    stagedRelax.diffuseMaxAccumulatedFrameNum = static_cast<uint32_t>(params.diffMaxAccumFrames + 0.5f);
    stagedRelax.specularMaxAccumulatedFrameNum = static_cast<uint32_t>(params.specMaxAccumFrames + 0.5f);
    stagedRelax.diffuseMaxFastAccumulatedFrameNum = static_cast<uint32_t>(params.diffMaxFastAccumFrames + 0.5f);
    stagedRelax.specularMaxFastAccumulatedFrameNum = static_cast<uint32_t>(params.specMaxFastAccumFrames + 0.5f);
    // NRD uploads historyFixFrameNum + 1; the port uploads the engine value directly
    stagedRelax.historyFixFrameNum = static_cast<uint32_t>(glm::clamp(params.historyFixFrameNum - 1.0f, 0.0f, 3.0f));
    stagedRelax.historyFixBasePixelStride = static_cast<uint32_t>(params.historyFixBasePixelStride + 0.5f);
    stagedRelax.historyFixAlternatePixelStride = static_cast<uint32_t>(params.historyFixBasePixelStride + 0.5f);
    stagedRelax.historyFixEdgeStoppingNormalPower = params.historyFixEdgeStoppingNormalPower;
    stagedRelax.fastHistoryClampingSigmaScale = params.fastHistoryClampingSigmaScale;
    // Checkerboard forces the prepass in the port; NRD always runs its prepass, radius 0 disables the blur
    stagedRelax.diffusePrepassBlurRadius = params.enablePrepass ? params.diffBlurRadius : 0.0f;
    stagedRelax.specularPrepassBlurRadius = params.enablePrepass ? params.specBlurRadius : 0.0f;
    // NRD uploads minHitDistanceWeight * 2; the port uploads the engine value directly
    stagedRelax.minHitDistanceWeight = params.minHitDistanceWeight * 0.5f;
    stagedRelax.spatialVarianceEstimationHistoryThreshold = static_cast<uint32_t>(params.spatialVarianceEstimationHistoryThreshold + 0.5f);
    stagedRelax.diffusePhiLuminance = params.diffPhiLuminance;
    stagedRelax.specularPhiLuminance = params.specPhiLuminance;
    stagedRelax.lobeAngleFraction = params.lobeAngleFraction;
    stagedRelax.roughnessFraction = params.roughnessFraction;
    stagedRelax.specularVarianceBoost = params.specVarianceBoost;
    stagedRelax.specularLobeAngleSlack = params.specLobeAngleSlack;
    stagedRelax.atrousIterationNum = static_cast<uint32_t>(glm::max(params.atrousIterations, 2));
    // NRD derives gMaxLuminanceRelativeDifference = -log(minLuminanceWeight); invert the engine's direct value
    stagedRelax.diffuseMinLuminanceWeight = glm::exp(-params.diffMaxLuminanceRelativeDifference);
    stagedRelax.specularMinLuminanceWeight = glm::exp(-params.specMaxLuminanceRelativeDifference);
    stagedRelax.depthThreshold = params.depthThreshold;
    stagedRelax.luminanceEdgeStoppingRelaxation = params.luminanceEdgeStoppingRelaxation;
    stagedRelax.normalEdgeStoppingRelaxation = params.normalEdgeStoppingRelaxation;
    stagedRelax.roughnessEdgeStoppingRelaxation = params.roughnessEdgeStoppingRelaxation;
    stagedRelax.enableAntiFirefly = params.enableAntiFirefly;
    stagedRelax.enableRoughnessEdgeStopping = params.roughnessEdgeStoppingEnabled;

    stagedReblur = nrd::ReblurSettings{};
    // NRD 4.17 dropped hitDistD (magic-curve normalization); the D knob only affects the port
    stagedReblur.hitDistanceParameters.A = reblurParams.hitDistA;
    stagedReblur.hitDistanceParameters.B = reblurParams.hitDistB;
    stagedReblur.hitDistanceParameters.C = reblurParams.hitDistC;
    stagedReblur.antilagSettings.luminanceSigmaScale = reblurParams.antilagLuminanceSigmaScale;
    stagedReblur.antilagSettings.luminanceSensitivity = reblurParams.antilagLuminanceSensitivity;
    stagedReblur.convergenceSettings.s = reblurParams.convergenceS;
    stagedReblur.convergenceSettings.b = reblurParams.convergenceB;
    stagedReblur.convergenceSettings.p = reblurParams.convergenceP;
    stagedReblur.maxAccumulatedFrameNum = static_cast<uint32_t>(reblurParams.maxAccumulatedFrameNum + 0.5f);
    stagedReblur.maxFastAccumulatedFrameNum = static_cast<uint32_t>(reblurParams.maxFastAccumulatedFrameNum + 0.5f);
    // NRD derives stabilizationStrength = N / (1 + N) from this; the stabilizationStrength knob is inert here
    stagedReblur.maxStabilizedFrameNum = reblurParams.enableTemporalStabilization ? static_cast<uint32_t>(reblurParams.maxStabilizedFrameNum + 0.5f) : 0;
    // Unlike RELAX, NRD uploads the REBLUR history-fix values directly (no +1)
    stagedReblur.historyFixFrameNum = static_cast<uint32_t>(reblurParams.historyFixFrameNum + 0.5f);
    stagedReblur.historyFixBasePixelStride = static_cast<uint32_t>(reblurParams.historyFixBasePixelStride + 0.5f);
    stagedReblur.historyFixAlternatePixelStride = static_cast<uint32_t>(reblurParams.historyFixBasePixelStride + 0.5f);
    stagedReblur.fastHistoryClampingSigmaScale = reblurParams.fastHistoryClampingSigmaScale;
    stagedReblur.diffusePrepassBlurRadius = reblurParams.enablePrepass ? reblurParams.diffusePrepassBlurRadius : 0.0f;
    stagedReblur.specularPrepassBlurRadius = reblurParams.enablePrepass ? reblurParams.specularPrepassBlurRadius : 0.0f;
    stagedReblur.minHitDistanceWeight = reblurParams.minHitDistanceWeight;
    stagedReblur.minBlurRadius = reblurParams.minBlurRadius;
    stagedReblur.maxBlurRadius = reblurParams.maxBlurRadius;
    stagedReblur.lobeAngleFraction = reblurParams.lobeAngleFraction;
    stagedReblur.roughnessFraction = reblurParams.roughnessFraction;
    stagedReblur.planeDistanceSensitivity = reblurParams.planeDistanceSensitivity;
    stagedReblur.fireflySuppressorMinRelativeScale = reblurParams.fireflySuppressorMinRelativeScale;
    stagedReblur.hitDistanceReconstructionMode = static_cast<nrd::HitDistanceReconstructionMode>(reblurParams.hitDistanceReconstructionMode);
    stagedReblur.enableAntiFirefly = reblurParams.enableAntiFirefly;
}

bool NrdDenoiser::Prepare(RenderGraph& graph,
                          const Core::ViewFamily& viewFamily,
                          Core::Array<uint32_t, 2> renderExtent,
                          NrdBackend backend,
                          const Core::RELAXParams& relaxParams,
                          const Core::ReBLURParams& reblurParams,
                          uint64_t frameNumber,
                          uint32_t frameInFlightIndex,
                          float renderFps)
{
    if (!EnsureInstance()) { return false; }
    EnsureResources(renderExtent, frameNumber);
    if (bInitFailed) { return false; }
    ReleaseRetired(frameNumber, false);

    activeBackend = backend;
    // Reset history after a gap (denoiser toggled off/on leaves stale pool content) or a backend switch (the inactive denoiser's history goes stale)
    const bool bHistoryReset = bPendingHistoryClear || backend != lastBackend || (lastRecordedFrame != UINT64_MAX && frameNumber != lastRecordedFrame + 1);
    lastBackend = backend;
    StageSettings(viewFamily, renderExtent, relaxParams, reblurParams, frameNumber, renderFps, bHistoryReset);
    bPendingHistoryClear = false;

    // FIF fence already waited; this frame slot's sets are GPU-idle
    VK_CHECK(vkResetDescriptorPool(context->device, frameDescriptorPools[frameInFlightIndex], 0));

    for (uint32_t i = 0; i < IO_COUNT; i++) {
        TrackedTexture& tex = ioTextures[i];
        // The RDG's end-of-frame final barrier left every previously used IO texture in GENERAL
        const VkImageLayout importLayout = tex.layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
        graph.ImportTexture(NRD_IO_SIDS[i], tex.image, tex.view,
                            TextureInfo{tex.format, tex.width, tex.height, 1},
                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                            importLayout, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VK_IMAGE_LAYOUT_GENERAL);
        tex.layout = VK_IMAGE_LAYOUT_GENERAL;
        tex.access = VK_ACCESS_2_NONE;
    }

    lastRecordedFrame = frameNumber;
    prevRectWidth = renderExtent[0];
    prevRectHeight = renderExtent[1];
    return true;
}

void NrdDenoiser::AddDispatchPass(RenderGraph& graph, ResourceManager* resourceManager, PipelineManager* pipelineManager, uint32_t frameInFlightIndex)
{
    const bool bReblur = activeBackend == NrdBackend::Reblur;
    auto& pass = graph.AddPass(bReblur ? SID("[NRD] ReBLUR Dispatch") : SID("[NRD] RELAX Dispatch"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::NRD);
    if (bReblur) {
        // REBLUR's temporal stabilization writes modified motion back into IN_MV
        pass.WriteStorageImage(NRD_IN_MV_SID);
    }
    else {
        pass.ReadSampledImage(NRD_IN_MV_SID);
    }
    pass.ReadSampledImage(NRD_IN_NORMAL_ROUGHNESS_SID);
    pass.ReadSampledImage(NRD_IN_VIEWZ_SID);
    pass.ReadSampledImage(NRD_IN_DIFF_SID);
    pass.ReadSampledImage(NRD_IN_SPEC_SID);
    pass.WriteStorageImage(NRD_OUT_DIFF_SID);
    pass.WriteStorageImage(NRD_OUT_SPEC_SID);
    pass.Execute([this, resourceManager, pipelineManager, frameInFlightIndex](VkCommandBuffer cmd, VulkanContext*, RenderGraph&) {
        RecordDispatches(cmd, resourceManager, pipelineManager, frameInFlightIndex);
    });
}

void NrdDenoiser::RebindEngineDescriptorBuffers(VkCommandBuffer cmd, ResourceManager* resourceManager, PipelineManager* pipelineManager)
{
    Core::Array<VkDescriptorBufferBindingInfoEXT, 3> bindings{
        resourceManager->bindlessSamplerTextureDescriptorBuffer.GetBindingInfo(),
        resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo(),
        resourceManager->bindlessRDGRTDescriptorBuffer.GetBindingInfo()
    };
    Core::Array<uint32_t, 3> indices{0u, 1u, 2u};
    Core::Array<VkDeviceSize, 3> offsets{0, 0, 0};
    vkCmdBindDescriptorBuffersEXT(cmd, bindings.Size(), bindings.Data());
    vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineManager->GetGlobalPipelineLayout(), 0, bindings.Size(), indices.Data(), offsets.Data());
    vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineManager->GetGlobalPipelineLayout(), 0, bindings.Size(), indices.Data(), offsets.Data());
}

NrdDenoiser::TrackedTexture* NrdDenoiser::ResolveResource(const nrd::ResourceDesc& resource)
{
    const nrd::InstanceDesc& desc = *nrd::GetInstanceDesc(*instance);
    switch (resource.type) {
        case nrd::ResourceType::PERMANENT_POOL: return &poolTextures[resource.indexInPool];
        case nrd::ResourceType::TRANSIENT_POOL: return &poolTextures[desc.permanentPoolSize + resource.indexInPool];
        case nrd::ResourceType::IN_MV: return &ioTextures[IO_IN_MV];
        case nrd::ResourceType::IN_NORMAL_ROUGHNESS: return &ioTextures[IO_IN_NORMAL_ROUGHNESS];
        case nrd::ResourceType::IN_VIEWZ: return &ioTextures[IO_IN_VIEWZ];
        case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST: return &ioTextures[IO_IN_DIFF_RADIANCE_HITDIST];
        case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST: return &ioTextures[IO_IN_SPEC_RADIANCE_HITDIST];
        case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST: return &ioTextures[IO_OUT_DIFF_RADIANCE_HITDIST];
        case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST: return &ioTextures[IO_OUT_SPEC_RADIANCE_HITDIST];
        default:
            LOG_ERROR(Renderer, "[NRD] Unexpected resource type {}", static_cast<uint32_t>(resource.type));
            return nullptr;
    }
}

void NrdDenoiser::RecordDispatches(VkCommandBuffer cmd, ResourceManager* resourceManager, PipelineManager* pipelineManager, uint32_t frameInFlightIndex)
{
    const bool bReblur = activeBackend == NrdBackend::Reblur;
    if (nrd::SetCommonSettings(*instance, stagedCommon) != nrd::Result::SUCCESS) {
        LOG_ERROR(Renderer, "[NRD] SetCommonSettings failed");
        return;
    }
    const void* settings = bReblur ? static_cast<const void*>(&stagedReblur) : static_cast<const void*>(&stagedRelax);
    const nrd::Identifier identifier = bReblur ? NRD_REBLUR_IDENTIFIER : NRD_RELAX_IDENTIFIER;
    if (nrd::SetDenoiserSettings(*instance, identifier, settings) != nrd::Result::SUCCESS) {
        LOG_ERROR(Renderer, "[NRD] SetDenoiserSettings failed");
        return;
    }

    const nrd::DispatchDesc* dispatches = nullptr;
    uint32_t dispatchNum = 0;
    if (nrd::GetComputeDispatches(*instance, &identifier, 1, dispatches, dispatchNum) != nrd::Result::SUCCESS) {
        LOG_ERROR(Renderer, "[NRD] GetComputeDispatches failed");
        return;
    }

    const nrd::InstanceDesc& desc = *nrd::GetInstanceDesc(*instance);
    const nrd::LibraryDesc& lib = *nrd::GetLibraryDesc();

    // States established by the RDG barriers around this pass (ReBLUR declares IN_MV as a storage write)
    for (uint32_t i = IO_IN_MV; i <= IO_IN_SPEC_RADIANCE_HITDIST; i++) {
        const bool bStorageEntry = bReblur && i == IO_IN_MV;
        ioTextures[i].layout = bStorageEntry ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ioTextures[i].access = bStorageEntry ? VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }
    for (uint32_t i = IO_OUT_DIFF_RADIANCE_HITDIST; i <= IO_OUT_SPEC_RADIANCE_HITDIST; i++) {
        ioTextures[i].layout = VK_IMAGE_LAYOUT_GENERAL;
        ioTextures[i].access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    }

    uint8_t* mapped = static_cast<uint8_t*>(constantRing.allocationInfo.pMappedData);
    const uint32_t frameBase = frameInFlightIndex * constantSlotsPerFrame * constantStride;
    uint32_t constantCursor = 0;
    uint32_t previousConstantOffset = frameBase;

    for (uint32_t d = 0; d < dispatchNum; d++) {
        const nrd::DispatchDesc& dispatch = dispatches[d];
        const nrd::PipelineDesc& pipelineDesc = desc.pipelines[dispatch.pipelineIndex];
        const PipelineObjects& pipeline = pipelines[dispatch.pipelineIndex];

        VkDescriptorSet resourceSet = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo setAllocInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = frameDescriptorPools[frameInFlightIndex],
            .descriptorSetCount = 1,
            .pSetLayouts = &pipeline.setLayout,
        };
        VK_CHECK(vkAllocateDescriptorSets(context->device, &setAllocInfo, &resourceSet));

        Core::InlineVector<VkDescriptorImageInfo, 40> imageInfos{};
        Core::InlineVector<VkWriteDescriptorSet, 40> writes{};
        Core::InlineVector<VkImageMemoryBarrier2, 40> barriers{};

        uint32_t resourceIndex = 0;
        for (uint32_t r = 0; r < pipelineDesc.resourceRangesNum; r++) {
            const nrd::ResourceRangeDesc& range = pipelineDesc.resourceRanges[r];
            const bool bStorage = range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;
            const uint32_t baseRegister = (bStorage ? lib.spirvBindingOffsets.storageTextureAndBufferOffset : lib.spirvBindingOffsets.textureOffset) + desc.resourcesBaseRegisterIndex;

            for (uint32_t j = 0; j < range.descriptorsNum; j++) {
                const nrd::ResourceDesc& resource = dispatch.resources[resourceIndex++];
                TrackedTexture* tex = ResolveResource(resource);
                if (tex == nullptr) {
                    RebindEngineDescriptorBuffers(cmd, resourceManager, pipelineManager);
                    return;
                }

                const VkImageLayout desiredLayout = bStorage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                const VkAccessFlags2 desiredAccess = bStorage ? (VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                const bool bStateChanged = tex->layout != desiredLayout || tex->access != desiredAccess;
                const bool bStorageToStorage = bStorage && (tex->access & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) != 0;
                if (bStateChanged || bStorageToStorage) {
                    barriers.PushBack(VkImageMemoryBarrier2{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        .srcAccessMask = tex->access,
                        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        .dstAccessMask = desiredAccess,
                        .oldLayout = tex->layout,
                        .newLayout = desiredLayout,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = tex->image,
                        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                    });
                    tex->layout = desiredLayout;
                    tex->access = desiredAccess;
                }

                imageInfos.PushBack(VkDescriptorImageInfo{VK_NULL_HANDLE, tex->view, desiredLayout});
                writes.PushBack(VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = resourceSet,
                    .dstBinding = baseRegister + j,
                    .descriptorCount = 1,
                    .descriptorType = bStorage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .pImageInfo = &imageInfos[imageInfos.Size() - 1],
                });
            }
        }

        if (dispatch.constantBufferDataSize > 0 && !dispatch.constantBufferDataMatchesPreviousDispatch) {
            const uint32_t offset = frameBase + constantCursor * constantStride;
            if (constantCursor < constantSlotsPerFrame) {
                memcpy(mapped + offset, dispatch.constantBufferData, dispatch.constantBufferDataSize);
                previousConstantOffset = offset;
                constantCursor++;
            }
            else {
                LOG_ERROR(Renderer, "[NRD] Constant ring overflow ({} slots)", constantSlotsPerFrame);
            }
        }
        const uint32_t dynamicOffset = previousConstantOffset;

        if (!barriers.IsEmpty()) {
            VkDependencyInfo dependencyInfo{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.Size()),
                .pImageMemoryBarriers = barriers.Data(),
            };
            vkCmdPipelineBarrier2(cmd, &dependencyInfo);
        }

        vkUpdateDescriptorSets(context->device, static_cast<uint32_t>(writes.Size()), writes.Data(), 0, nullptr);

        const Core::Array<VkDescriptorSet, 2> sets{resourceSet, sharedSet};
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout, 0, static_cast<uint32_t>(sets.Size()), sets.Data(), 1, &dynamicOffset);
        vkCmdDispatch(cmd, dispatch.gridWidth, dispatch.gridHeight, 1);
    }

    // Restore the states the RDG assumes this pass left the user-facing textures in
    Core::InlineVector<VkImageMemoryBarrier2, IO_COUNT> restoreBarriers{};
    for (uint32_t i = 0; i < IO_COUNT; i++) {
        TrackedTexture& tex = ioTextures[i];
        const bool bOutput = i == IO_OUT_DIFF_RADIANCE_HITDIST || i == IO_OUT_SPEC_RADIANCE_HITDIST || (bReblur && i == IO_IN_MV);
        const VkImageLayout expectedLayout = bOutput ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (tex.layout != expectedLayout) {
            const VkAccessFlags2 expectedAccess = bOutput ? VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            restoreBarriers.PushBack(VkImageMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = tex.access,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = expectedAccess,
                .oldLayout = tex.layout,
                .newLayout = expectedLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = tex.image,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            });
            tex.layout = expectedLayout;
            tex.access = expectedAccess;
        }
    }
    if (!restoreBarriers.IsEmpty()) {
        VkDependencyInfo dependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = static_cast<uint32_t>(restoreBarriers.Size()),
            .pImageMemoryBarriers = restoreBarriers.Data(),
        };
        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }

    RebindEngineDescriptorBuffers(cmd, resourceManager, pipelineManager);
}

void SetupNRDPrepPasses(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, NrdBackend backend, const Core::ReBLURParams& reblurParams)
{
    ZoneScoped;
    const uint32_t width = renderExtent[0];
    const uint32_t height = renderExtent[1];
    const StringID gbufferOne = targets.gbufferOne;
    const StringID depth = targets.depthCopy;
    const StringID diffInput = targets.intermediateOne;
    const StringID specInput = targets.intermediateTwo;

    {
        auto& pass = graph.AddPass(SID("[NRD] Prep Guides"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::NRD);
        pass.ReadBuffer(SCENE_DATA_BUFFER);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.WriteStorageImage(NRD_IN_NORMAL_ROUGHNESS_SID);
        pass.WriteStorageImage(NRD_IN_MV_SID);
        pass.WriteStorageImage(NRD_IN_VIEWZ_SID);
        pass.Execute([pipelineManager, gbufferOne, depth, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            NrdPrepGuidesPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .sceneDataIndex = 0,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .outNormalRoughnessIndex = graph.GetStorageImageViewDescriptorIndex(NRD_IN_NORMAL_ROUGHNESS_SID),
                .outMvIndex = graph.GetStorageImageViewDescriptorIndex(NRD_IN_MV_SID),
                .outViewZIndex = graph.GetStorageImageViewDescriptorIndex(NRD_IN_VIEWZ_SID),
                .rectSize = {width, height},
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("nrd_prep_guides"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    if (backend == NrdBackend::Reblur) {
        const glm::vec4 hitDistParams{reblurParams.hitDistA, reblurParams.hitDistB, reblurParams.hitDistC, 0.0f};
        auto& pass = graph.AddPass(SID("[NRD] Prep Radiance"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::NRD);
        pass.ReadSampledImage(diffInput);
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(NRD_IN_VIEWZ_SID);
        pass.WriteStorageImage(NRD_IN_DIFF_SID);
        pass.WriteStorageImage(NRD_IN_SPEC_SID);
        pass.Execute([pipelineManager, diffInput, specInput, gbufferOne, hitDistParams, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            NrdReblurRadiancePackPushConstant pc{
                .hitDistParams = hitDistParams,
                .rectSize = {width, height},
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(NRD_IN_VIEWZ_SID),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(NRD_IN_DIFF_SID),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(NRD_IN_SPEC_SID),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("nrd_reblur_radiance_pack"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }
    else {
        auto& pass = graph.AddPass(SID("[NRD] Prep Radiance"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::NRD);
        pass.ReadSampledImage(diffInput);
        pass.ReadSampledImage(specInput);
        pass.WriteStorageImage(NRD_IN_DIFF_SID);
        pass.WriteStorageImage(NRD_IN_SPEC_SID);
        pass.Execute([pipelineManager, diffInput, specInput, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            NrdRadianceCopyPushConstant pc{
                .rectSize = {width, height},
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(NRD_IN_DIFF_SID),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(NRD_IN_SPEC_SID),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("nrd_radiance_copy"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }
}

void SetupNRDOutputPass(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, NrdBackend backend)
{
    ZoneScoped;
    const uint32_t width = renderExtent[0];
    const uint32_t height = renderExtent[1];
    const StringID diffOutput = targets.intermediateOne;
    const StringID specOutput = targets.intermediateTwo;
    const StringID srcDiff = NRD_OUT_DIFF_SID;
    const StringID srcSpec = NRD_OUT_SPEC_SID;
    // REBLUR outputs YCoCg + normalized hitT; remodulate only consumes .rgb, so the writeback just converts color
    const StringID copyPipeline = backend == NrdBackend::Reblur ? SID("nrd_reblur_output_copy") : SID("nrd_radiance_copy");

    auto& pass = graph.AddPass(SID("[NRD] Output Writeback"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::NRD);
    pass.ReadSampledImage(srcDiff);
    pass.ReadSampledImage(srcSpec);
    pass.WriteStorageImage(diffOutput);
    pass.WriteStorageImage(specOutput);
    pass.Execute([pipelineManager, copyPipeline, srcDiff, srcSpec, diffOutput, specOutput, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        NrdRadianceCopyPushConstant pc{
            .rectSize = {width, height},
            .diffIndex = graph.GetSampledImageViewDescriptorIndex(srcDiff),
            .specIndex = graph.GetSampledImageViewDescriptorIndex(srcSpec),
            .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(diffOutput),
            .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(specOutput),
        };
        const PipelineEntry* p = pipelineManager->GetPipelineEntry(copyPipeline);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
        vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
    });
}
} // Render
