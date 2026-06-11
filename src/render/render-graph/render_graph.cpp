//
// Created by William on 2025-12-27.
//

#include "render_graph.h"

#include <utility>
#include <bit>

#include "render_graph_config.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "render_pass.h"
#include "core/containers/arena_array.h"
#include "core/containers/arena_string.h"
#include "core/containers/inline_queue.h"
#include "core/containers/inline_vector.h"
#include "engine/logging/engine_assert.h"
#include "engine/logging/engine_log.h"
#include "render/resource_manager.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace Render
{
RenderGraph::RenderGraph(VulkanContext* context, ResourceManager* resourceManager, Core::TlsfAllocator& alloc, Core::Arena& arena, RenderGraphAllocFns inAllocFns)
    : context(context),
      resourceManager(resourceManager),
      alloc(&alloc),
      arena(&arena),
      allocFns(std::move(inAllocFns)),
      physicalResources(&alloc, Core::AllocTag::Render, 256),
      waveOffsets(&alloc, Core::AllocTag::Render),
      compiledImageBarriers(&alloc, Core::AllocTag::Render),
      compiledBufferBarriers(&alloc, Core::AllocTag::Render),
      compiledWaveRanges(&alloc, Core::AllocTag::Render),
      textureCarryovers(&alloc, Core::AllocTag::Render),
      bufferCarryovers(&alloc, Core::AllocTag::Render)
{
    ENGINE_ASSERT(Renderer, context != nullptr, "RenderGraph requires a valid VulkanContext");
    ENGINE_ASSERT(Renderer, context->allocator != nullptr, "RenderGraph requires an initialized VMA allocator");
    for (int32_t i = 0; i < uploadArenas.Size(); ++i) {
        VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.usage = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;
        bufferInfo.size = RDG_DEFAULT_UPLOAD_LINEAR_ALLOCATOR_SIZE;
        VmaAllocationCreateInfo vmaAllocInfo = {};
        vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        auto uploadAlloc = allocFns.createBuffer(context, bufferInfo, vmaAllocInfo);
        uploadArenas[i].buffer = uploadAlloc.buffer;
        uploadArenas[i].bufferAllocation = uploadAlloc.allocation;
        uploadArenas[i].mappedData = uploadAlloc.mappedData; {
            auto debugName = Core::InlineString<>("rdgFrameBufferUploader_");
            debugName.Append(i);
            allocFns.setDebugName(context, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(uploadArenas[i].buffer), debugName.c_str());
        }

        bufferInfo.usage = VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;
        vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        auto readbackAlloc = allocFns.createBuffer(context, bufferInfo, vmaAllocInfo);
        meshletCountReadbacks[i].buffer = readbackAlloc.buffer;
        meshletCountReadbacks[i].bufferAllocation = readbackAlloc.allocation;
        meshletCountReadbacks[i].mappedData = readbackAlloc.mappedData; {
            auto readbackDebugName = Core::InlineString("rdgReadbackBuffer__");
            readbackDebugName.Append(i);
            allocFns.setDebugName(context, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(meshletCountReadbacks[i].buffer), readbackDebugName.c_str());
        }
    }
}

RenderGraph::~RenderGraph()
{
    for (auto& phys : physicalResources) {
        DestroyPhysicalResource(phys);
    }
    for (auto& _arena : uploadArenas) {
        if (_arena.buffer != VK_NULL_HANDLE) {
            allocFns.destroyBuffer(context, _arena.buffer, _arena.bufferAllocation);
        }
    }
    for (auto& readback : meshletCountReadbacks) {
        if (readback.buffer != VK_NULL_HANDLE) {
            allocFns.destroyBuffer(context, readback.buffer, readback.bufferAllocation);
        }
    }
    for (auto& entry : persistentBuffers) {
        for (auto& slot : entry.slots) {
            if (slot.buffer != VK_NULL_HANDLE) {
                if (slot.userData != 0 && entry.onDestroyUserData) { entry.onDestroyUserData(slot.userData); }
                allocFns.destroyBuffer(context, slot.buffer, slot.allocation);
            }
        }
    }
}

RenderPass& RenderGraph::AddPass(StringID passId, VkPipelineStageFlags2 stages, ResourceCategory category)
{
    auto* pass = new(arena->AllocRaw(sizeof(RenderPass), alignof(RenderPass))) RenderPass(*this, passId, stages, category, arena);
    passes.PushBack(pass);
    return *pass;
}

void RenderGraph::AccumulateUsage()
{
    for (auto& pass : passes) {
        for (const uint32_t texIndex : pass->storageImageWrites) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }

        for (const uint32_t texIndex : pass->storageImageReads) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }

        for (const uint32_t texIndex : pass->sampledImageReads) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        for (const uint32_t texIndex : pass->imageReadWrite) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        for (const uint32_t texIndex : pass->clearImageWrites) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        for (const uint32_t texIndex : pass->blitImageReads) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        for (const uint32_t texIndex : pass->blitImageWrites) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        for (const uint32_t texIndex : pass->copyImageReads) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        for (const uint32_t texIndex : pass->copyImageWrites) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        for (const uint32_t texIndex : pass->colorAttachments) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }

        if (pass->depthStencilAttachment != UINT_MAX) {
            auto& tex = textures[pass->depthStencilAttachment];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferWrites) {
            buffers[bufIndex].accumulatedUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferReadWrite) {
            buffers[bufIndex].accumulatedUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferReads) {
            buffers[bufIndex].accumulatedUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferTransferWrites) {
            buffers[bufIndex].accumulatedUsage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferTransferReads) {
            buffers[bufIndex].accumulatedUsage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferIndexRead) {
            buffers[bufIndex].accumulatedUsage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferIndirectReads) {
            buffers[bufIndex].accumulatedUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferIndirectCountReads) {
            buffers[bufIndex].accumulatedUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferTLASWrites) {
            buffers[bufIndex].accumulatedUsage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferTLASReads) {
            buffers[bufIndex].accumulatedUsage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferScratchWrites) {
            buffers[bufIndex].accumulatedUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferASInputReads) {
            buffers[bufIndex].accumulatedUsage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        }
    }

    for (auto& tex : textures) {
        if (tex.clear.has_value()) {
            tex.accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
    }
}

void RenderGraph::BuildDependencyEdges()
{
    struct TextureEpoch
    {
        uint32_t lastWriter = UINT32_MAX;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        Core::ArenaVector<uint32_t> readers; // active readers in the current layout epoch
        Core::ArenaVector<uint32_t> causes; // passes that established this epoch; new same-epoch readers get a RAW edge from each
    };

    struct BufferEpoch
    {
        uint32_t lastWriter = UINT32_MAX;
        Core::ArenaVector<uint32_t> readers;
    };

    auto texEpochs = Core::ArenaArray<TextureEpoch>(arena, RDG_MAX_TEXTURES);
    for (auto& t : texEpochs) {
        t.readers = Core::ArenaVector<uint32_t>(arena);
        t.causes = Core::ArenaVector<uint32_t>(arena);
    }

    auto bufEpochs = Core::ArenaArray<BufferEpoch>(arena, RDG_MAX_BUFFERS);
    for (auto& b : bufEpochs) { b.readers = Core::ArenaVector<uint32_t>(arena); }

    auto addEdge = [&](uint32_t from, uint32_t to) {
        passes[from]->outEdges.PushBack(to);
        passes[to]->inEdges.PushBack(from);
        passes[to]->inDegree++;
    };

    for (uint32_t passIdx = 0; passIdx < passes.Size(); passIdx++) {
        auto& pass = passes[passIdx];

        auto trackTexture = [&](Core::Span<uint32_t> indices, VkImageLayout targetLayout, bool isWrite) {
            for (uint32_t resIdx : indices) {
                auto& t = texEpochs[resIdx];

                if (isWrite) {
                    // WAR: every active reader must finish before this write.
                    for (uint32_t rdr : t.readers) { addEdge(rdr, passIdx); }
                    // WAW: the previous writer must finish before this write.
                    if (t.lastWriter != UINT32_MAX) { addEdge(t.lastWriter, passIdx); }
                    t.readers.Clear();
                    t.causes.Clear();
                    t.lastWriter = passIdx;
                    t.layout = VK_IMAGE_LAYOUT_UNDEFINED;
                }
                else if (!t.readers.IsEmpty() && t.layout == targetLayout) {
                    // Same epoch: RAW from whoever established this layout.
                    for (uint32_t cause : t.causes) { addEdge(cause, passIdx); }
                    t.readers.PushBack(passIdx);
                }
                else {
                    // New epoch (layout change, or first read after a write).
                    // Wait for all active readers of the old layout, or the last writer if no readers.
                    if (!t.readers.IsEmpty()) {
                        for (uint32_t rdr : t.readers) { addEdge(rdr, passIdx); }
                        t.causes.Clear();
                        for (uint32_t rdr : t.readers) { t.causes.PushBack(rdr); }
                    }
                    else if (t.lastWriter != UINT32_MAX) {
                        addEdge(t.lastWriter, passIdx);
                        t.causes.Clear();
                        t.causes.PushBack(t.lastWriter);
                    }
                    t.readers.Clear();
                    t.readers.PushBack(passIdx);
                    t.layout = targetLayout;
                }
            }
        };

        auto trackBuffer = [&](Core::Span<uint32_t> indices, bool isWrite) {
            for (uint32_t resIdx : indices) {
                auto& b = bufEpochs[resIdx];
                if (isWrite) {
                    // WAR: every active reader must finish before this write.
                    for (uint32_t rdr : b.readers) { addEdge(rdr, passIdx); }
                    // WAW: the previous writer must finish before this write.
                    if (b.lastWriter != UINT32_MAX) { addEdge(b.lastWriter, passIdx); }
                    b.readers.Clear();
                    b.lastWriter = passIdx;
                }
                else {
                    // RAW: must wait for the last writer.
                    if (b.lastWriter != UINT32_MAX) { addEdge(b.lastWriter, passIdx); }
                    b.readers.PushBack(passIdx);
                }
            }
        };

        // Textures
        trackTexture(pass->storageImageReads, VK_IMAGE_LAYOUT_GENERAL, false);
        trackTexture(pass->sampledImageReads, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);
        trackTexture(pass->blitImageReads, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, false);
        trackTexture(pass->copyImageReads, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, false);
        trackTexture(pass->imageReadWrite, VK_IMAGE_LAYOUT_GENERAL, true);
        trackTexture(pass->clearImageWrites, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
        trackTexture(pass->storageImageWrites, VK_IMAGE_LAYOUT_GENERAL, true);
        trackTexture(pass->blitImageWrites, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
        trackTexture(pass->copyImageWrites, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
        trackTexture(pass->colorAttachments, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);

        if (pass->depthStencilAttachment != UINT_MAX) {
            uint32_t texIndex = pass->depthStencilAttachment;
            constexpr VkImageLayout depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            const bool isWrite = (pass->depthAccessType & DepthAccessType::Write) != DepthAccessType::None;
            trackTexture(Core::Span{&texIndex, 1}, depthLayout, isWrite);
        }

        // Buffers
        trackBuffer(pass->bufferReads, false);
        trackBuffer(pass->bufferTransferReads, false);
        trackBuffer(pass->bufferIndexRead, false);
        trackBuffer(pass->bufferIndirectReads, false);
        trackBuffer(pass->bufferIndirectCountReads, false);
        trackBuffer(pass->bufferReadWrite, true);
        trackBuffer(pass->bufferWrites, true);
        trackBuffer(pass->bufferTransferWrites, true);
        trackBuffer(pass->bufferTLASWrites, true);
        trackBuffer(pass->bufferScratchWrites, true);
        trackBuffer(pass->bufferTLASReads, false);
        trackBuffer(pass->bufferASInputReads, false);
    }
}

void RenderGraph::TopologicalSortPasses()
{
    Core::InlineQueue<uint32_t, RDG_MAX_PASSES> zeroDegreeQueue;

    for (uint32_t passIdx = 0; passIdx < passes.Size(); passIdx++) {
        auto& pass = passes[passIdx];
        pass->passIndex = passIdx;
        if (pass->inDegree == 0) {
            zeroDegreeQueue.Push(passIdx);
        }
    }

    // Kahn's Topolohical Sort
    while (zeroDegreeQueue.Size() > 0) {
        const uint32_t currPassIdx = zeroDegreeQueue.Pop();
        const auto& currPass = passes[currPassIdx];
        sortedPasses.PushBack(currPass);

        for (const uint32_t neighborIdx : currPass->outEdges) {
            passes[neighborIdx]->inDegree--;
            if (passes[neighborIdx]->inDegree == 0) {
                zeroDegreeQueue.Push(neighborIdx);
            }
        }
    }

    ENGINE_ASSERT(Renderer, sortedPasses.Size() == passes.Size(), "Render graph cycle detected");

    if (bDebugLogging) {
        LOG_INFO(Renderer, "=== RDG Topological Sort ===");
        for (uint32_t i = 0; i < sortedPasses.Size(); i++) {
            const RenderPass* pass = sortedPasses[i];
            LOG_INFO(Renderer, "  [{}] {} (was {})", i, pass->renderPassId.ToString(), pass->passIndex);
        }
    }

    AssignWaveIndices();

    if (bDebugLogging) {
        LOG_INFO(Renderer, "=== RDG Waves ===");
        for (uint32_t w = 0; w + 1 < waveOffsets.Size(); w++) {
            const uint32_t start = waveOffsets[w];
            const uint32_t end = waveOffsets[w + 1];
            LOG_INFO(Renderer, "  Wave {}:", w);
            for (uint32_t i = start; i < end; i++) {
                LOG_INFO(Renderer, "    [{}] {}", i, sortedPasses[i]->renderPassId.ToString());
            }
        }

        ExportGraphviz();
    }
}

void RenderGraph::AssignWaveIndices()
{
    waveOffsets.Clear();

    for (uint32_t i = 0; i < sortedPasses.Size(); i++) {
        RenderPass* pass = sortedPasses[i];
        pass->passIndex = i;

        uint32_t wave = 0;
        for (const uint32_t predIdx : pass->inEdges) {
            wave = std::max(wave, passes[predIdx]->waveIndex + 1);
        }
        pass->waveIndex = wave;

        if (wave >= waveOffsets.Size()) {
            waveOffsets.PushBack(i);
        }
    }
    waveOffsets.PushBack(static_cast<uint32_t>(sortedPasses.Size()));
}

void RenderGraph::CalculateLifetimes()
{
    for (uint32_t passIdx = 0; passIdx < sortedPasses.Size(); passIdx++) {
        auto& pass = sortedPasses[passIdx];

        auto UpdateTextureLifetime = [passIdx, &pass](TextureResource& tex) {
            tex.firstPass = std::min(tex.firstPass, passIdx);
            tex.lastPass = std::max(tex.lastPass, passIdx);
            tex.category |= pass->category;
        };

        auto UpdateBufferLifetime = [passIdx, &pass](BufferResource& buf) {
            buf.firstPass = std::min(buf.firstPass, passIdx);
            buf.lastPass = std::max(buf.lastPass, passIdx);
            buf.category |= pass->category;
        };

        for (const uint32_t texIndex : pass->storageImageWrites) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->storageImageReads) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->sampledImageReads) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->imageReadWrite) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->clearImageWrites) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->blitImageWrites) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->blitImageReads) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->copyImageReads) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->copyImageWrites) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->colorAttachments) { UpdateTextureLifetime(textures[texIndex]); }
        if (pass->depthStencilAttachment != UINT_MAX) { UpdateTextureLifetime(textures[pass->depthStencilAttachment]); }

        for (const uint32_t bufIndex : pass->bufferReads) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferWrites) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferReadWrite) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferTransferReads) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferTransferWrites) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferIndexRead) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferIndirectReads) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferIndirectCountReads) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferASInputReads) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferTLASWrites) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferScratchWrites) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferTLASReads) { UpdateBufferLifetime(buffers[bufIndex]); }
    }
}

void RenderGraph::PopulateAutoClearTextures()
{
    for (auto& tex : textures) {
        if (!tex.clear.has_value()) { continue; }
        if (tex.firstPass == UINT32_MAX) { continue; }
        sortedPasses[tex.firstPass]->autoClearTextures.PushBack(tex.index);
    }
}

void RenderGraph::AssignPhysicalResources(uint64_t currentFrame)
{
    for (auto& tex : textures) {
        if (tex.accumulatedUsage == 0) {
            if (bDebugLogging) {
                LOG_WARN(Renderer, "Texture '{}' created but never used", tex.textureId.ToString());
            }
            continue;
        }
        if (!tex.HasPhysical()) {
            // Build desired dimensions for this texture
            ResourceDimensions desiredDim;
            desiredDim.type = ResourceDimensions::Type::Image;
            desiredDim.format = tex.textureInfo.format;
            desiredDim.width = tex.textureInfo.width;
            desiredDim.height = tex.textureInfo.height;
            desiredDim.depth = 1;
            desiredDim.levels = tex.textureInfo.mipLevels;
            desiredDim.layers = 1;
            desiredDim.samples = 1;
            desiredDim.imageUsage = tex.accumulatedUsage;
            desiredDim.resourceId = tex.textureId;

            // Try to find existing physical resource with matching dimensions
            bool foundAlias = false;
            for (uint32_t i = 0; i < physicalResources.Size(); i++) {
                auto& phys = physicalResources[i];
                if (phys.bIsImported) { continue; }
                if (!phys.bCanAlias) { continue; }
                if (phys.dimensions != desiredDim) { continue; }

                // Cross-frame resources can't alias at all.
                // Well, not strictly true. If a texture is carried over to next frame, it can be aliased if the other use is before the texture's first pass.
                // But we can't reasonably infer that information from the current frame, so we will reject cross-frame aliasing.
                if (!tex.bCanUseAliasedTexture && !phys.logicalResourceIndices.IsEmpty()) {
                    continue;
                }

                // If already allocated, must be superset (greedy first-fit search)
                if (phys.IsAllocated()) {
                    if ((phys.dimensions.imageUsage & tex.accumulatedUsage) != tex.accumulatedUsage) {
                        continue;
                    }
                }

                bool canAlias = true;
                for (uint32_t logicalIdx : phys.logicalResourceIndices) {
                    auto& existing = textures[logicalIdx];

                    bool passOverlap = !(tex.lastPass < existing.firstPass || existing.lastPass < tex.firstPass);
                    bool waveOverlap = false;
                    if (tex.firstPass != UINT32_MAX && tex.lastPass != UINT32_MAX && existing.firstPass != UINT32_MAX && existing.lastPass != UINT32_MAX) {
                        uint32_t texFirstWave = sortedPasses[tex.firstPass]->waveIndex;
                        uint32_t texLastWave = sortedPasses[tex.lastPass]->waveIndex;
                        uint32_t existingFirstWave = sortedPasses[existing.firstPass]->waveIndex;
                        uint32_t existingLastWave = sortedPasses[existing.lastPass]->waveIndex;
                        waveOverlap = !(texLastWave < existingFirstWave || existingLastWave < texFirstWave);
                    }

                    if (passOverlap || waveOverlap) {
                        canAlias = false;
                        break;
                    }
                }

                if (canAlias) {
                    tex.physicalIndex = i;
                    if (!phys.IsAllocated()) {
                        phys.dimensions.imageUsage |= tex.accumulatedUsage;
                    }
                    phys.logicalResourceIndices.PushBack(tex.index);
                    phys.bCanAlias = tex.bCanUseAliasedTexture;
                    phys.bIsViewportScaled |= tex.bIsViewportScaled;
                    phys.category |= tex.category;
                    AppendUsageChain(phys, tex.textureId, tex.bCanUseAliasedTexture, bDebugLogging);
                    foundAlias = true;
                    break;
                }
            }

            if (!foundAlias) {
                // No alias found, allocate new physical resource
                tex.physicalIndex = physicalResources.Size();
                physicalResources.EmplaceBack();
                auto& newPhys = physicalResources.Back();
                newPhys.dimensions = desiredDim;
                newPhys.logicalResourceIndices.PushBack(tex.index);
                newPhys.bCanAlias = tex.bCanUseAliasedTexture;
                newPhys.bIsViewportScaled = tex.bIsViewportScaled;
                newPhys.category = tex.category;
                AppendUsageChain(newPhys, tex.textureId, tex.bCanUseAliasedTexture, bDebugLogging);
            }
        }
    }

    for (auto& tex : textures) {
        if (tex.accumulatedUsage == 0) { continue; }

        auto& phys = physicalResources[tex.physicalIndex];
        if (!phys.IsAllocated() && tex.textureInfo.format != VK_FORMAT_UNDEFINED) {
            CreatePhysicalImage(phys, phys.dimensions);
        }
        phys.lastUsedFrame = currentFrame;
    }

    for (auto& buf : buffers) {
        if (buf.accumulatedUsage == 0) {
            if (bDebugLogging) {
                LOG_WARN(Renderer, "Buffer '{}' created but never used", buf.bufferId.ToString());
            }
            continue;
        }

        if (!buf.HasPhysical()) {
            ResourceDimensions desiredDim;
            desiredDim.type = ResourceDimensions::Type::Buffer;
            desiredDim.bufferSize = buf.bufferInfo.size;
            desiredDim.bufferUsage = buf.accumulatedUsage;
            desiredDim.bufferMinAlignment = buf.minAlignment;
            desiredDim.resourceId = buf.bufferId;

            bool foundAlias = false;
            for (uint32_t i = 0; i < physicalResources.Size(); i++) {
                auto& phys = physicalResources[i];

                if (phys.bIsImported) { continue; }
                if (!phys.bCanAlias) { continue; }
                if (!phys.dimensions.IsBuffer()) { continue; }

                if (phys.dimensions.bufferSize != desiredDim.bufferSize) { continue; }
                if (phys.dimensions.bufferMinAlignment != desiredDim.bufferMinAlignment) { continue; }

                // Cross-frame resources can't alias at all.
                if (!buf.bCanUseAliasedBuffer && !phys.logicalResourceIndices.IsEmpty()) {
                    continue;
                }

                // If already allocated, must be superset (greedy search)
                if (phys.IsAllocated()) {
                    if ((phys.dimensions.bufferUsage & buf.accumulatedUsage) != buf.accumulatedUsage) {
                        continue;
                    }
                }

                bool canAlias = true;
                for (uint32_t logicalIdx : phys.logicalResourceIndices) {
                    auto& existing = buffers[logicalIdx];

                    bool passOverlap = !(buf.lastPass < existing.firstPass || existing.lastPass < buf.firstPass);
                    bool waveOverlap = false;
                    if (buf.firstPass != UINT32_MAX && buf.lastPass != UINT32_MAX && existing.firstPass != UINT32_MAX && existing.lastPass != UINT32_MAX) {
                        uint32_t bufFirstWave = sortedPasses[buf.firstPass]->waveIndex;
                        uint32_t bufLastWave = sortedPasses[buf.lastPass]->waveIndex;
                        uint32_t existingFirstWave = sortedPasses[existing.firstPass]->waveIndex;
                        uint32_t existingLastWave = sortedPasses[existing.lastPass]->waveIndex;
                        waveOverlap = !(bufLastWave < existingFirstWave || existingLastWave < bufFirstWave);
                    }

                    if (passOverlap || waveOverlap) {
                        canAlias = false;
                        break;
                    }
                }

                if (canAlias) {
                    buf.physicalIndex = i;
                    phys.logicalResourceIndices.PushBack(buf.index);
                    if (!phys.IsAllocated()) {
                        phys.dimensions.bufferUsage |= buf.accumulatedUsage;
                    }
                    phys.bCanAlias = buf.bCanUseAliasedBuffer;
                    phys.bIsViewportScaled |= buf.bIsViewportScaled;
                    phys.category |= buf.category;
                    AppendUsageChain(phys, buf.bufferId, buf.bCanUseAliasedBuffer, bDebugLogging);
                    foundAlias = true;
                    break;
                }
            }

            if (!foundAlias) {
                buf.physicalIndex = physicalResources.Size();
                physicalResources.EmplaceBack();
                auto& newPhys = physicalResources.Back();
                newPhys.dimensions = desiredDim;
                newPhys.logicalResourceIndices.PushBack(buf.index);
                newPhys.bCanAlias = buf.bCanUseAliasedBuffer;
                newPhys.bIsViewportScaled = buf.bIsViewportScaled;
                newPhys.category = buf.category;
                AppendUsageChain(newPhys, buf.bufferId, buf.bCanUseAliasedBuffer, bDebugLogging);
            }
        }
    }

    for (auto& buf : buffers) {
        if (buf.accumulatedUsage == 0) { continue; }

        auto& phys = physicalResources[buf.physicalIndex];
        if (!phys.IsAllocated() && buf.bufferInfo.size > 0) {
            CreatePhysicalBuffer(phys, phys.dimensions);
        }
        phys.lastUsedFrame = currentFrame;
    }

    for (auto& phys : physicalResources) {
        if (phys.NeedsDescriptorWrite() && phys.imageView != VK_NULL_HANDLE) {
            if (allocFns.writeDescriptors) {
                allocFns.writeDescriptors(phys);
                phys.descriptorWritten = true;
                continue;
            }
            if ((phys.dimensions.imageUsage & VK_IMAGE_USAGE_SAMPLED_BIT) == VK_IMAGE_USAGE_SAMPLED_BIT) {
                ImageChannelType sampledChannelType = GetImageChannelType(phys.dimensions.format, phys.aspect);
                switch (sampledChannelType) {
                    case ImageChannelType::Float4:
                        phys.sampledDescriptorHandle = transientSampledImageHandleAllocator.Add();
                        ENGINE_ASSERT(Renderer, phys.sampledDescriptorHandle.IsValid(), "Sampled descriptor handle pool exhausted (Float4)");
                        resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledImageDescriptor(
                            phys.sampledDescriptorHandle.index, {nullptr, phys.imageView, VK_IMAGE_LAYOUT_GENERAL}
                        );
                        break;
                    case ImageChannelType::Float2:
                        phys.sampledDescriptorHandle = transientSampledFloat2HandleAllocator.Add();
                        ENGINE_ASSERT(Renderer, phys.sampledDescriptorHandle.IsValid(), "Sampled descriptor handle pool exhausted (Float2)");
                        resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledFloat2Descriptor(
                            phys.sampledDescriptorHandle.index, {nullptr, phys.imageView, VK_IMAGE_LAYOUT_GENERAL}
                        );
                        break;
                    case ImageChannelType::Float:
                        phys.sampledDescriptorHandle = transientSampledFloatHandleAllocator.Add();
                        ENGINE_ASSERT(Renderer, phys.sampledDescriptorHandle.IsValid(), "Sampled descriptor handle pool exhausted (Float)");
                        resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledFloatDescriptor(
                            phys.sampledDescriptorHandle.index, {nullptr, phys.imageView, VK_IMAGE_LAYOUT_GENERAL}
                        );
                        break;
                    case ImageChannelType::UInt4:
                        phys.sampledDescriptorHandle = transientSampledUInt4HandleAllocator.Add();
                        ENGINE_ASSERT(Renderer, phys.sampledDescriptorHandle.IsValid(), "Sampled descriptor handle pool exhausted (UInt4)");
                        resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledUInt4Descriptor(
                            phys.sampledDescriptorHandle.index, {nullptr, phys.imageView, VK_IMAGE_LAYOUT_GENERAL}
                        );
                        break;
                    case ImageChannelType::UInt2:
                        phys.sampledDescriptorHandle = transientSampledUInt2HandleAllocator.Add();
                        ENGINE_ASSERT(Renderer, phys.sampledDescriptorHandle.IsValid(), "Sampled descriptor handle pool exhausted (UInt2)");
                        resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledUInt2Descriptor(
                            phys.sampledDescriptorHandle.index, {nullptr, phys.imageView, VK_IMAGE_LAYOUT_GENERAL}
                        );
                        break;
                    case ImageChannelType::UInt:
                        phys.sampledDescriptorHandle = transientSampledUIntHandleAllocator.Add();
                        ENGINE_ASSERT(Renderer, phys.sampledDescriptorHandle.IsValid(), "Sampled descriptor handle pool exhausted (UInt)");
                        resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledUIntDescriptor(
                            phys.sampledDescriptorHandle.index, {nullptr, phys.imageView, VK_IMAGE_LAYOUT_GENERAL}
                        );
                        break;
                }
            }

            if (phys.depthOnlyView != VK_NULL_HANDLE) {
                phys.depthOnlyDescriptorHandle = transientSampledFloatHandleAllocator.Add();
                ENGINE_ASSERT(Renderer, phys.depthOnlyDescriptorHandle.IsValid(), "Depth-only descriptor handle pool exhausted");
                resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledFloatDescriptor(
                    phys.depthOnlyDescriptorHandle.index, {nullptr, phys.depthOnlyView, VK_IMAGE_LAYOUT_GENERAL}
                );
            }

            if (phys.stencilOnlyView != VK_NULL_HANDLE) {
                phys.stencilOnlyDescriptorHandle = transientStorageUIntHandleAllocator.Add();
                ENGINE_ASSERT(Renderer, phys.stencilOnlyDescriptorHandle.IsValid(), "Stencil-only descriptor handle pool exhausted");
                resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledUIntDescriptor(
                    phys.stencilOnlyDescriptorHandle.index,
                    {nullptr, phys.stencilOnlyView, VK_IMAGE_LAYOUT_GENERAL}
                );
            }

            if ((phys.dimensions.imageUsage & VK_IMAGE_USAGE_STORAGE_BIT) == VK_IMAGE_USAGE_STORAGE_BIT) {
                ImageChannelType storageType = GetImageChannelType(phys.dimensions.format, phys.aspect);
                for (uint32_t mip = 0; mip < phys.dimensions.levels; ++mip) {
                    switch (storageType) {
                        case ImageChannelType::Float4:
                        {
                            phys.storageMipDescriptorHandles[mip] = transientStorageFloat4HandleAllocator.Add();
                            ENGINE_ASSERT(Renderer, phys.storageMipDescriptorHandles[mip].IsValid(), "Storage mip descriptor handle pool exhausted (Float4)");
                            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageFloat4Descriptor(
                                phys.storageMipDescriptorHandles[mip].index,
                                {nullptr, phys.mipViews[mip], VK_IMAGE_LAYOUT_GENERAL}
                            );
                            break;
                        }
                        case ImageChannelType::Float2:
                        {
                            phys.storageMipDescriptorHandles[mip] = transientStorageFloat2HandleAllocator.Add();
                            ENGINE_ASSERT(Renderer, phys.storageMipDescriptorHandles[mip].IsValid(), "Storage mip descriptor handle pool exhausted (Float2)");
                            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageFloat2Descriptor(
                                phys.storageMipDescriptorHandles[mip].index,
                                {nullptr, phys.mipViews[mip], VK_IMAGE_LAYOUT_GENERAL}
                            );
                            break;
                        }
                        case ImageChannelType::Float:
                        {
                            phys.storageMipDescriptorHandles[mip] = transientStorageFloatHandleAllocator.Add();
                            ENGINE_ASSERT(Renderer, phys.storageMipDescriptorHandles[mip].IsValid(), "Storage mip descriptor handle pool exhausted (Float)");
                            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageFloatDescriptor(
                                phys.storageMipDescriptorHandles[mip].index,
                                {nullptr, phys.mipViews[mip], VK_IMAGE_LAYOUT_GENERAL}
                            );
                            break;
                        }
                        case ImageChannelType::UInt4:
                        {
                            phys.storageMipDescriptorHandles[mip] = transientStorageUInt4HandleAllocator.Add();
                            ENGINE_ASSERT(Renderer, phys.storageMipDescriptorHandles[mip].IsValid(), "Storage mip descriptor handle pool exhausted (UInt4)");
                            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageUInt4Descriptor(
                                phys.storageMipDescriptorHandles[mip].index,
                                {nullptr, phys.mipViews[mip], VK_IMAGE_LAYOUT_GENERAL}
                            );
                            break;
                        }
                        case ImageChannelType::UInt2:
                        {
                            phys.storageMipDescriptorHandles[mip] = transientStorageUInt2HandleAllocator.Add();
                            ENGINE_ASSERT(Renderer, phys.storageMipDescriptorHandles[mip].IsValid(), "Storage mip descriptor handle pool exhausted (UInt2)");
                            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageUInt2Descriptor(
                                phys.storageMipDescriptorHandles[mip].index,
                                {nullptr, phys.mipViews[mip], VK_IMAGE_LAYOUT_GENERAL}
                            );
                            break;
                        }
                        case ImageChannelType::UInt:
                        {
                            phys.storageMipDescriptorHandles[mip] = transientStorageUIntHandleAllocator.Add();
                            ENGINE_ASSERT(Renderer, phys.storageMipDescriptorHandles[mip].IsValid(), "Storage mip descriptor handle pool exhausted (UInt)");
                            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageUIntDescriptor(
                                phys.storageMipDescriptorHandles[mip].index,
                                {nullptr, phys.mipViews[mip], VK_IMAGE_LAYOUT_GENERAL}
                            );
                            break;
                        }
                    }
                }
            }

            phys.descriptorWritten = true;
        }

        if (phys.NeedsAddressRetrieval()) {
            phys.bufferAddress = allocFns.getBufferDeviceAddress(context, phys.buffer);
            phys.addressRetrieved = true;
        }
    }
}

void RenderGraph::PrecomputeBarriers()
{
    compiledImageBarriers.Clear();
    compiledBufferBarriers.Clear();
    compiledWaveRanges.Clear();

    if (physicalResources.IsEmpty()) { return; }

    struct WaveLocalState
    {
        VkPipelineStageFlags2 stages{0};
        VkAccessFlags2 access{0};
        bool wasWritten{false};
    };
    auto waveState = Core::ArenaArray<WaveLocalState>(arena, physicalResources.Size());

    auto GetPhysical = [this](uint32_t texIndex) -> PhysicalResource& {
        return physicalResources[textures[texIndex].physicalIndex];
    };

    for (uint32_t waveIdx = 0; waveIdx + 1 < waveOffsets.Size(); waveIdx++) {
        const uint32_t waveStart = waveOffsets[waveIdx];
        const uint32_t waveEnd = waveOffsets[waveIdx + 1];

        // Reset wave-local state
        for (uint32_t i = 0; i < physicalResources.Size(); i++) { waveState[i] = {}; }

        // AutoClears: push pre-clear barriers into the flat array, advance state so wave barrier sees correct oldLayout
        const uint32_t preClearImageStart = static_cast<uint32_t>(compiledImageBarriers.Size());
        for (uint32_t pi = waveStart; pi < waveEnd; pi++) {
            RenderPass* pass = sortedPasses[pi];
            for (const uint32_t texIndex : pass->autoClearTextures) {
                auto& tex = textures[texIndex];
                auto& phys = GetPhysical(texIndex);
                compiledImageBarriers.PushBack(VkHelpers::ImageMemoryBarrier(
                    phys.image, VkHelpers::SubresourceRange(phys.aspect),
                    phys.event.stages, phys.event.access, tex.layout,
                    VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                ));
                tex.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                phys.event.stages = VK_PIPELINE_STAGE_2_CLEAR_BIT;
                phys.event.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            }
        }
        const uint32_t preClearImageCount = static_cast<uint32_t>(compiledImageBarriers.Size()) - preClearImageStart;

        // Record wave barrier range start
        const uint32_t imageStart = static_cast<uint32_t>(compiledImageBarriers.Size());
        const uint32_t bufferStart = static_cast<uint32_t>(compiledBufferBarriers.Size());

        auto addImageBarrier = [&](const VkImageMemoryBarrier2& b) {
            for (uint32_t i = imageStart; i < compiledImageBarriers.Size(); i++) {
                auto& e = compiledImageBarriers[i];
                if (e.image == b.image && e.newLayout == b.newLayout) {
                    e.srcStageMask |= b.srcStageMask;
                    e.srcAccessMask |= b.srcAccessMask;
                    e.dstStageMask |= b.dstStageMask;
                    e.dstAccessMask |= b.dstAccessMask;
                    return;
                }
            }
            compiledImageBarriers.PushBack(b);
        };

        auto addBufferBarrier = [&](const VkBufferMemoryBarrier2& b) {
            for (uint32_t i = bufferStart; i < compiledBufferBarriers.Size(); i++) {
                auto& e = compiledBufferBarriers[i];
                if (e.buffer == b.buffer) {
                    e.srcStageMask |= b.srcStageMask;
                    e.srcAccessMask |= b.srcAccessMask;
                    e.dstStageMask |= b.dstStageMask;
                    e.dstAccessMask |= b.dstAccessMask;
                    return;
                }
            }
            compiledBufferBarriers.PushBack(b);
        };

        // Collect barriers from all passes in this wave
        for (uint32_t pi = waveStart; pi < waveEnd; pi++) {
            RenderPass* pass = sortedPasses[pi];

            for (const uint32_t texIndex : pass->colorAttachments) {
                auto& tex = textures[texIndex];
                auto& phys = GetPhysical(texIndex);
                addImageBarrier(VkHelpers::ImageMemoryBarrier(phys.image, VkHelpers::SubresourceRange(phys.aspect),
                                                              phys.event.stages, phys.event.access, tex.layout,
                                                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                                                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
                LogImageBarrier(tex.textureId, compiledImageBarriers.Back(), tex.physicalIndex);
            }

            if (pass->depthStencilAttachment != UINT_MAX) {
                const uint32_t texIndex = pass->depthStencilAttachment;
                auto& tex = textures[texIndex];
                auto& phys = GetPhysical(texIndex);
                VkPipelineStageFlags2 dstStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                VkAccessFlags2 dstAccess = 0;
                if ((pass->depthAccessType & DepthAccessType::Read) != DepthAccessType::None) { dstAccess |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT; }
                if ((pass->depthAccessType & DepthAccessType::Write) != DepthAccessType::None) { dstAccess |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; }
                addImageBarrier(VkHelpers::ImageMemoryBarrier(phys.image, VkHelpers::SubresourceRange(phys.aspect),
                                                              phys.event.stages, phys.event.access, tex.layout, dstStages, dstAccess,
                                                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL));
                LogImageBarrier(tex.textureId, compiledImageBarriers.Back(), tex.physicalIndex);
            }

            for (const uint32_t texIndex : pass->storageImageWrites) {
                auto& tex = textures[texIndex];
                auto& phys = GetPhysical(texIndex);
                addImageBarrier(VkHelpers::ImageMemoryBarrier(phys.image, VkHelpers::SubresourceRange(phys.aspect),
                                                              phys.event.stages, phys.event.access, tex.layout,
                                                              pass->stages, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL));
                LogImageBarrier(tex.textureId, compiledImageBarriers.Back(), tex.physicalIndex);
            }

            for (const uint32_t texIndex : pass->storageImageReads) {
                auto& tex = textures[texIndex];
                auto& phys = GetPhysical(texIndex);
                addImageBarrier(VkHelpers::ImageMemoryBarrier(phys.image, VkHelpers::SubresourceRange(phys.aspect),
                                                              phys.event.stages, phys.event.access, tex.layout,
                                                              pass->stages, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL));
                LogImageBarrier(tex.textureId, compiledImageBarriers.Back(), tex.physicalIndex);
            }

            for (const uint32_t texIndex : pass->sampledImageReads) {
                auto& tex = textures[texIndex];
                auto& phys = GetPhysical(texIndex);
                addImageBarrier(VkHelpers::ImageMemoryBarrier(phys.image, VkHelpers::SubresourceRange(phys.aspect),
                                                              phys.event.stages, phys.event.access, tex.layout,
                                                              pass->stages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
                LogImageBarrier(tex.textureId, compiledImageBarriers.Back(), tex.physicalIndex);
            }

            for (const uint32_t texIndex : pass->imageReadWrite) {
                auto& tex = textures[texIndex];
                auto& phys = GetPhysical(texIndex);
                addImageBarrier(VkHelpers::ImageMemoryBarrier(phys.image, VkHelpers::SubresourceRange(phys.aspect),
                                                              phys.event.stages, phys.event.access, tex.layout,
                                                              pass->stages,
                                                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                                              VK_IMAGE_LAYOUT_GENERAL));
                LogImageBarrier(tex.textureId, compiledImageBarriers.Back(), tex.physicalIndex);
            }

            for (const uint32_t texIndex : pass->blitImageReads) {
                auto& tex = textures[texIndex];
                auto& phys = GetPhysical(texIndex);
                addImageBarrier(VkHelpers::ImageMemoryBarrier(phys.image, VkHelpers::SubresourceRange(phys.aspect),
                                                              phys.event.stages, phys.event.access, tex.layout,
                                                              VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL));
                LogImageBarrier(tex.textureId, compiledImageBarriers.Back(), tex.physicalIndex);
            }

            for (const uint32_t texIndex : pass->clearImageWrites) {
                auto& tex = textures[texIndex];
                auto& phys = GetPhysical(texIndex);
                addImageBarrier(VkHelpers::ImageMemoryBarrier(phys.image, VkHelpers::SubresourceRange(phys.aspect),
                                                              phys.event.stages, phys.event.access, tex.layout,
                                                              VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
                LogImageBarrier(tex.textureId, compiledImageBarriers.Back(), tex.physicalIndex);
            }

            for (const uint32_t texIndex : pass->blitImageWrites) {
                auto& tex = textures[texIndex];
                auto& phys = GetPhysical(texIndex);
                addImageBarrier(VkHelpers::ImageMemoryBarrier(phys.image, VkHelpers::SubresourceRange(phys.aspect),
                                                              phys.event.stages, phys.event.access, tex.layout,
                                                              VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
                LogImageBarrier(tex.textureId, compiledImageBarriers.Back(), tex.physicalIndex);
            }

            for (const uint32_t texIndex : pass->copyImageReads) {
                auto& tex = textures[texIndex];
                auto& phys = GetPhysical(texIndex);
                addImageBarrier(VkHelpers::ImageMemoryBarrier(phys.image, VkHelpers::SubresourceRange(phys.aspect),
                                                              phys.event.stages, phys.event.access, tex.layout,
                                                              VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL));
                LogImageBarrier(tex.textureId, compiledImageBarriers.Back(), tex.physicalIndex);
            }

            for (const uint32_t texIndex : pass->copyImageWrites) {
                auto& tex = textures[texIndex];
                auto& phys = GetPhysical(texIndex);
                addImageBarrier(VkHelpers::ImageMemoryBarrier(phys.image, VkHelpers::SubresourceRange(phys.aspect),
                                                              phys.event.stages, phys.event.access, tex.layout,
                                                              VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
                LogImageBarrier(tex.textureId, compiledImageBarriers.Back(), tex.physicalIndex);
            }

            for (const uint32_t bufIndex : pass->bufferWrites) {
                auto& buf = buffers[bufIndex];
                auto& phys = physicalResources[buf.physicalIndex];
                if (phys.bDisableBarriers) { continue; }
                addBufferBarrier({
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                    phys.event.stages, phys.event.access, pass->stages, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, phys.buffer, 0, VK_WHOLE_SIZE
                });
                LogBufferBarrier(buf.bufferId, VK_ACCESS_2_SHADER_WRITE_BIT);
            }

            for (const uint32_t bufIndex : pass->bufferReadWrite) {
                auto& buf = buffers[bufIndex];
                auto& phys = physicalResources[buf.physicalIndex];
                if (phys.bDisableBarriers) { continue; }
                addBufferBarrier({
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                    phys.event.stages, phys.event.access, pass->stages, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, phys.buffer, 0, VK_WHOLE_SIZE
                });
                LogBufferBarrier(buf.bufferId, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
            }

            for (const uint32_t bufIndex : pass->bufferTransferWrites) {
                auto& buf = buffers[bufIndex];
                auto& phys = physicalResources[buf.physicalIndex];
                if (phys.bDisableBarriers) { continue; }
                addBufferBarrier({
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                    phys.event.stages, phys.event.access, pass->stages, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, phys.buffer, 0, VK_WHOLE_SIZE
                });
                LogBufferBarrier(buf.bufferId, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            }

            for (const uint32_t bufIndex : pass->bufferTransferReads) {
                auto& buf = buffers[bufIndex];
                auto& phys = physicalResources[buf.physicalIndex];
                if (phys.bDisableBarriers) { continue; }
                addBufferBarrier({
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                    phys.event.stages, phys.event.access, pass->stages, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, phys.buffer, 0, VK_WHOLE_SIZE
                });
                LogBufferBarrier(buf.bufferId, VK_ACCESS_2_TRANSFER_READ_BIT);
            }

            for (const uint32_t bufIndex : pass->bufferReads) {
                auto& buf = buffers[bufIndex];
                auto& phys = physicalResources[buf.physicalIndex];
                if (phys.bDisableBarriers) { continue; }
                addBufferBarrier({
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                    phys.event.stages, phys.event.access, pass->stages, VK_ACCESS_2_SHADER_READ_BIT,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, phys.buffer, 0, VK_WHOLE_SIZE
                });
                LogBufferBarrier(buf.bufferId, VK_ACCESS_2_SHADER_READ_BIT);
            }

            for (const uint32_t bufIndex : pass->bufferIndexRead) {
                auto& buf = buffers[bufIndex];
                auto& phys = physicalResources[buf.physicalIndex];
                if (phys.bDisableBarriers) { continue; }
                addBufferBarrier({
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                    phys.event.stages, phys.event.access, VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, phys.buffer, 0, VK_WHOLE_SIZE
                });
                LogBufferBarrier(buf.bufferId, VK_ACCESS_2_INDEX_READ_BIT);
            }

            for (const uint32_t bufIndex : pass->bufferIndirectReads) {
                auto& buf = buffers[bufIndex];
                auto& phys = physicalResources[buf.physicalIndex];
                if (phys.bDisableBarriers) { continue; }
                addBufferBarrier({
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                    phys.event.stages, phys.event.access,
                    pass->stages | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                    VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, phys.buffer, 0, VK_WHOLE_SIZE
                });
                LogBufferBarrier(buf.bufferId, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT);
            }

            for (const uint32_t bufIndex : pass->bufferIndirectCountReads) {
                auto& buf = buffers[bufIndex];
                auto& phys = physicalResources[buf.physicalIndex];
                if (phys.bDisableBarriers) { continue; }
                addBufferBarrier({
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                    phys.event.stages, phys.event.access,
                    VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, phys.buffer, 0, VK_WHOLE_SIZE
                });
                LogBufferBarrier(buf.bufferId, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
            }

            for (const uint32_t bufIndex : pass->bufferTLASWrites) {
                auto& buf = buffers[bufIndex];
                auto& phys = physicalResources[buf.physicalIndex];
                if (phys.bDisableBarriers) { continue; }
                addBufferBarrier({
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                    phys.event.stages, phys.event.access,
                    VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, phys.buffer, 0, VK_WHOLE_SIZE
                });
                LogBufferBarrier(buf.bufferId, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
            }

            for (const uint32_t bufIndex : pass->bufferTLASReads) {
                auto& buf = buffers[bufIndex];
                auto& phys = physicalResources[buf.physicalIndex];
                if (phys.bDisableBarriers) { continue; }
                addBufferBarrier({
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                    phys.event.stages, phys.event.access,
                    pass->stages, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, phys.buffer, 0, VK_WHOLE_SIZE
                });
                LogBufferBarrier(buf.bufferId, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
            }

            for (const uint32_t bufIndex : pass->bufferScratchWrites) {
                auto& buf = buffers[bufIndex];
                auto& phys = physicalResources[buf.physicalIndex];
                if (phys.bDisableBarriers) { continue; }
                addBufferBarrier({
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                    phys.event.stages, phys.event.access,
                    VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, phys.buffer, 0, VK_WHOLE_SIZE
                });
                LogBufferBarrier(buf.bufferId, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
            }

            for (const uint32_t bufIndex : pass->bufferASInputReads) {
                auto& buf = buffers[bufIndex];
                auto& phys = physicalResources[buf.physicalIndex];
                if (phys.bDisableBarriers) { continue; }
                addBufferBarrier({
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                    phys.event.stages, phys.event.access,
                    VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, phys.buffer, 0, VK_WHOLE_SIZE
                });
                LogBufferBarrier(buf.bufferId, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
            }
        }

        compiledWaveRanges.PushBack({
            preClearImageStart, preClearImageCount,
            imageStart, static_cast<uint32_t>(compiledImageBarriers.Size()) - imageStart,
            bufferStart, static_cast<uint32_t>(compiledBufferBarriers.Size()) - bufferStart
        });

        // Update phys.event and tex.layout for all accesses in this wave.
        // Writes stamp directly; reads OR into the physical event only if no write claimed it.
        for (uint32_t pi = waveStart; pi < waveEnd; pi++) {
            RenderPass* pass = sortedPasses[pi];

            auto stampWrite = [&](uint32_t physIdx, VkPipelineStageFlags2 stage, VkAccessFlags2 access) {
                waveState[physIdx] = {stage, access, true};
            };
            auto stampRead = [&](uint32_t physIdx, VkPipelineStageFlags2 stage, VkAccessFlags2 access) {
                if (!waveState[physIdx].wasWritten) {
                    waveState[physIdx].stages |= stage;
                    waveState[physIdx].access |= access;
                }
            };

            for (const uint32_t i : pass->colorAttachments) {
                textures[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                stampWrite(textures[i].physicalIndex, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            }

            if (pass->depthStencilAttachment != UINT_MAX) {
                const uint32_t i = pass->depthStencilAttachment;
                textures[i].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                VkAccessFlags2 acc = 0;
                if ((pass->depthAccessType & DepthAccessType::Read) != DepthAccessType::None) { acc |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT; }
                if ((pass->depthAccessType & DepthAccessType::Write) != DepthAccessType::None) { acc |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; }
                const bool isWrite = (pass->depthAccessType & DepthAccessType::Write) != DepthAccessType::None;
                const VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                if (isWrite) { stampWrite(textures[i].physicalIndex, stage, acc); }
                else { stampRead(textures[i].physicalIndex, stage, acc); }
            }

            for (const uint32_t i : pass->storageImageWrites) {
                textures[i].layout = VK_IMAGE_LAYOUT_GENERAL;
                stampWrite(textures[i].physicalIndex, pass->stages, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            }
            for (const uint32_t i : pass->imageReadWrite) {
                textures[i].layout = VK_IMAGE_LAYOUT_GENERAL;
                stampWrite(textures[i].physicalIndex, pass->stages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            }
            for (const uint32_t i : pass->clearImageWrites) {
                textures[i].layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                stampWrite(textures[i].physicalIndex, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            }
            for (const uint32_t i : pass->blitImageWrites) {
                textures[i].layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                stampWrite(textures[i].physicalIndex, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            }
            for (const uint32_t i : pass->copyImageWrites) {
                textures[i].layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                stampWrite(textures[i].physicalIndex, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            }

            for (const uint32_t i : pass->storageImageReads) {
                textures[i].layout = VK_IMAGE_LAYOUT_GENERAL;
                stampRead(textures[i].physicalIndex, pass->stages, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            }
            for (const uint32_t i : pass->sampledImageReads) {
                textures[i].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                stampRead(textures[i].physicalIndex, pass->stages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            }
            for (const uint32_t i : pass->blitImageReads) {
                textures[i].layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                stampRead(textures[i].physicalIndex, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
            }
            for (const uint32_t i : pass->copyImageReads) {
                textures[i].layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                stampRead(textures[i].physicalIndex, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
            }

            for (const uint32_t b : pass->bufferWrites) { waveState[buffers[b].physicalIndex] = {pass->stages, VK_ACCESS_2_SHADER_WRITE_BIT, true}; }
            for (const uint32_t b : pass->bufferReadWrite) { waveState[buffers[b].physicalIndex] = {pass->stages, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, true}; }
            for (const uint32_t b : pass->bufferTransferWrites) { waveState[buffers[b].physicalIndex] = {pass->stages, VK_ACCESS_2_TRANSFER_WRITE_BIT, true}; }
            for (const uint32_t b : pass->bufferReads) { stampRead(buffers[b].physicalIndex, pass->stages, VK_ACCESS_2_SHADER_READ_BIT); }
            for (const uint32_t b : pass->bufferTransferReads) { stampRead(buffers[b].physicalIndex, pass->stages, VK_ACCESS_2_TRANSFER_READ_BIT); }
            for (const uint32_t b : pass->bufferIndexRead) { stampRead(buffers[b].physicalIndex, VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT); }
            for (const uint32_t b : pass->bufferIndirectReads) { stampRead(buffers[b].physicalIndex, pass->stages | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT); }
            for (const uint32_t b : pass->bufferIndirectCountReads) { stampRead(buffers[b].physicalIndex, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT); }
            for (const uint32_t b : pass->bufferTLASWrites) { waveState[buffers[b].physicalIndex] = {VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, true}; }
            for (const uint32_t b : pass->bufferScratchWrites) { waveState[buffers[b].physicalIndex] = {VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, true}; }
            for (const uint32_t b : pass->bufferTLASReads) { stampRead(buffers[b].physicalIndex, pass->stages, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR); }
            for (const uint32_t b : pass->bufferASInputReads) { stampRead(buffers[b].physicalIndex, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT); }
        }

        // Flush wave-local state into phys.event
        for (uint32_t physIdx = 0; physIdx < physicalResources.Size(); physIdx++) {
            const auto& s = waveState[physIdx];
            if (s.stages != 0) {
                physicalResources[physIdx].event.stages = s.stages;
                physicalResources[physIdx].event.access = s.access;
            }
        }
    }
}

void RenderGraph::Compile(uint64_t currentFrame)
{
    AccumulateUsage();

    BuildDependencyEdges();

    TopologicalSortPasses();

    CalculateLifetimes();

    PopulateAutoClearTextures();

    AssignPhysicalResources(currentFrame);

    PrecomputeBarriers();
}

void RenderGraph::Execute(VkCommandBuffer cmd)
{
    ZoneScoped;

    if (bDebugLogging) {
        LOG_INFO(Renderer, "=== RenderGraph Execution ===");
    }

    auto GetPhysical = [this](uint32_t texIndex) -> PhysicalResource& {
        return physicalResources[textures[texIndex].physicalIndex];
    };

    for (uint32_t waveIdx = 0; waveIdx < compiledWaveRanges.Size(); waveIdx++) {
        ZoneScopedN("Wave");
        const uint32_t waveStart = waveOffsets[waveIdx];
        const uint32_t waveEnd = waveOffsets[waveIdx + 1];
        const WaveBarrierRange& range = compiledWaveRanges[waveIdx];

        if (bDebugLogging) {
            LOG_INFO(Renderer, "[WAVE {}] passes {}-{}", waveIdx, waveStart, waveEnd - 1);
        }

        // AutoClears: one barrier batch for the whole wave, then issue clear commands
        if (range.preClearImageCount > 0) {
            ZoneScopedN("AutoClear");
            VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depInfo.imageMemoryBarrierCount = range.preClearImageCount;
            depInfo.pImageMemoryBarriers = compiledImageBarriers.Data() + range.preClearImageStart;
            allocFns.cmdPipelineBarrier2(cmd, &depInfo);

            for (uint32_t pi = waveStart; pi < waveEnd; pi++) {
                RenderPass* pass = sortedPasses[pi];
                for (const uint32_t texIndex : pass->autoClearTextures) {
                    auto& tex = textures[texIndex];
                    auto& phys = GetPhysical(texIndex);
                    VkImageSubresourceRange subRange = VkHelpers::SubresourceRange(phys.aspect);
                    if (phys.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) {
                        allocFns.cmdClearDepthStencilImage(cmd, phys.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &tex.clear.value().depthStencil, 1, &subRange);
                    }
                    else {
                        allocFns.cmdClearColorImage(cmd, phys.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &tex.clear.value().color, 1, &subRange);
                    }
                }
            }
        }

        // Submit the precomputed wave barrier
        if (range.imageCount > 0 || range.bufferCount > 0) {
            ZoneScopedN("PipelineBarrier");
            if (bDebugLogging) {
                LOG_INFO(Renderer, "  Wave {} barrier: {} image, {} buffer", waveIdx, range.imageCount, range.bufferCount);
            }
            VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depInfo.imageMemoryBarrierCount = range.imageCount;
            depInfo.pImageMemoryBarriers = compiledImageBarriers.Data() + range.imageStart;
            depInfo.bufferMemoryBarrierCount = range.bufferCount;
            depInfo.pBufferMemoryBarriers = compiledBufferBarriers.Data() + range.bufferStart;
            allocFns.cmdPipelineBarrier2(cmd, &depInfo);
        }

        // Execute all passes in this wave
        for (uint32_t pi = waveStart; pi < waveEnd; pi++) {
            RenderPass* pass = sortedPasses[pi];
            if (bDebugLogging) {
                LOG_INFO(Renderer, "  [PASS] {}", pass->renderPassId.ToString());
            }
            if (pass->executeFunc) {
                ZoneScopedN("Execute");
                ZoneText(pass->renderPassId.ToString(), strlen(pass->renderPassId.ToString()));
#ifdef WDEBUG
                VkDebugUtilsLabelEXT label = {};
                label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
                label.pLabelName = pass->renderPassId.ToString();
                allocFns.cmdBeginDebugUtilsLabel(cmd, &label);
#endif
                pass->executeFunc(cmd);
#ifdef WDEBUG
                allocFns.cmdEndDebugUtilsLabel(cmd);
#endif
            }
        }
    }

    //
    {
        ZoneScopedN("FinalBarriers");
        if (bDebugLogging) {
            LOG_INFO(Renderer, "[FINAL BARRIERS]");
        }
        Core::InlineVector<VkImageMemoryBarrier2, 16> finalBarriers;
        for (auto& tex : textures) {
            if (tex.HasPhysical() && tex.HasFinalLayout()) {
                auto& phys = physicalResources[tex.physicalIndex];
                if (tex.layout != tex.finalLayout) {
                    auto finalBarrier = VkHelpers::ImageMemoryBarrier(
                        phys.image, VkHelpers::SubresourceRange(phys.aspect),
                        phys.event.stages, phys.event.access, tex.layout,
                        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_NONE, tex.finalLayout
                    );
                    LogImageBarrier(tex.textureId, finalBarrier, tex.physicalIndex);
                    finalBarriers.PushBack(finalBarrier);
                    tex.layout = tex.finalLayout;
                }
            }
        }
        if (!finalBarriers.IsEmpty()) {
            VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(finalBarriers.Size());
            depInfo.pImageMemoryBarriers = finalBarriers.Data();
            allocFns.cmdPipelineBarrier2(cmd, &depInfo);
        }
    }
}

void RenderGraph::PrepareSwapchain(VkCommandBuffer cmd, StringID textureId)
{
    uint32_t* idx = textureNameToIndex.Find(textureId);
    if (!idx) {
        LOG_ERROR(Renderer, "[RenderGraph::PrepareSwapchain] Prepare swapchain failed.");
        return;
    }

    TextureResource& swapchainTexture = textures[*idx];
    auto& phys = physicalResources[swapchainTexture.physicalIndex];

    VkImageMemoryBarrier2 presentBarrier = VkHelpers::ImageMemoryBarrier(
        phys.image,
        VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
        phys.event.stages, phys.event.access, swapchainTexture.layout,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );

    VkDependencyInfo depInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &presentBarrier;
    allocFns.cmdPipelineBarrier2(cmd, &depInfo);
}

void RenderGraph::Reset(uint32_t _currentFrameIndex, uint64_t currentFrame, uint64_t maxFramesUnused)
{
    ZoneScoped;

    currentFrameIndex = _currentFrameIndex;
    uploadArenas[currentFrameIndex].allocator.Reset();

    //
    if (bDestroyViewportAssociated) {
        ZoneScopedN("DestroyViewportScaledResources");

        // Drop carryovers whose src physical is viewport-scaled
        for (int32_t i = static_cast<int32_t>(textureCarryovers.Size()) - 1; i >= 0; --i) {
            const TextureFrameCarryover& carryover = textureCarryovers[i];
            const TextureResource* tex = GetTexture(carryover.srcName);
            if (tex && tex->HasPhysical() && physicalResources[tex->physicalIndex].bIsViewportScaled) {
                textureCarryovers.SwapRemove(i);
            }
        }

        for (int32_t i = static_cast<int32_t>(bufferCarryovers.Size()) - 1; i >= 0; --i) {
            const BufferFrameCarryover& carryover = bufferCarryovers[i];
            const BufferResource* buf = GetBuffer(carryover.srcName);
            if (buf && buf->HasPhysical() && physicalResources[buf->physicalIndex].bIsViewportScaled) {
                bufferCarryovers.SwapRemove(i);
            }
        }
    }

    //
    {
        ZoneScopedN("CarryoverCapture");
        for (TextureFrameCarryover& carryover : textureCarryovers) {
            if (const TextureResource* tex = GetTexture(carryover.srcName)) {
                const PhysicalResource& phys = physicalResources[tex->physicalIndex];
                carryover.physicalImage = phys.image;
                carryover.textInfo = tex->textureInfo;
                carryover.layout = tex->layout;
                carryover.accumulatedUsage = tex->accumulatedUsage;
            }
        }
        for (BufferFrameCarryover& carryover : bufferCarryovers) {
            if (const BufferResource* buf = GetBuffer(carryover.srcName)) {
                const PhysicalResource& phys = physicalResources[buf->physicalIndex];
                carryover.buffer = phys.buffer;
                carryover.bufferInfo = buf->bufferInfo;
                carryover.accumulatedUsage = buf->accumulatedUsage;
            }
        }
        for (auto& entry : persistentBuffers) {
            PersistentBuffer& slot = entry.slots[currentFrameIndex];
            if (slot.buffer == VK_NULL_HANDLE) { continue; }
            if (const BufferResource* buf = GetBuffer(entry.name)) {
                slot.lastState = GetBufferState(entry.name);
            }
        }
    }

    //
    {
        ZoneScopedN("ClearContainers");
        for (RenderPass* pass : passes) {
            pass->~RenderPass();
        }
        passes.Clear();
        textures.Clear();
        textureNameToIndex.Clear();
        buffers.Clear();
        bufferNameToIndex.Clear();
        arena->Reset();
        passes = Core::ArenaFixedVector<RenderPass*>(arena, RDG_MAX_PASSES);
        sortedPasses = Core::ArenaFixedVector<RenderPass*>(arena, RDG_MAX_PASSES);
        textures = Core::ArenaFixedVector<TextureResource>(arena, RDG_MAX_TEXTURES);
        textureNameToIndex = Core::ArenaFixedMap<StringID, uint32_t>(arena, RDG_MAX_TEXTURES);
        buffers = Core::ArenaFixedVector<BufferResource>(arena, RDG_MAX_BUFFERS);
        bufferNameToIndex = Core::ArenaFixedMap<StringID, uint32_t>(arena, RDG_MAX_BUFFERS);

        for (auto& phys : physicalResources) {
            phys.logicalResourceIndices.Clear();
            phys.bCanAlias = true;
            if (!bDebugLogging) { phys.usageChain.Clear(); }
        }

        if (bDebugLogging) {
            debugCaptureFramesLeft--;
            if (debugCaptureFramesLeft == 0) {
                LOG_INFO(Renderer, "=== Physical Resource Aliasing Chains (2-frame) ===");
                for (uint32_t i = 0; i < physicalResources.Size(); i++) {
                    auto& phys = physicalResources[i];
                    if (!phys.usageChain.IsEmpty()) {
                        LOG_INFO(Renderer, "  Phys[{}]: {}", i, phys.usageChain.c_str());
                    }
                    phys.usageChain.Clear();
                }
                bDebugLogging = false;
            }
        }
    }

    //
    {
        ZoneScopedN("CleanupUnusedPhysicalResources");
        for (int i = static_cast<int>(physicalResources.Size()) - 1; i >= 0; --i) {
            auto& phys = physicalResources[i];

            if (bRemoveSwapchainPhysicals && phys.bIsSwapchain) {
                physicalResources.RemoveAt(i);
                continue;
            }

            if (phys.bIsImported) { continue; }
            if (!phys.IsAllocated()) { continue; }

            if (bDestroyViewportAssociated && phys.bIsViewportScaled) {
                DestroyPhysicalResource(phys);
                physicalResources.RemoveAt(i);
            }
            else if (currentFrame - phys.lastUsedFrame > maxFramesUnused) {
                DestroyPhysicalResource(phys);
                physicalResources.RemoveAt(i);
            }
        }
    }

    //
    {
        ZoneScopedN("CarryoverTextureRestoration");
        for (auto& carryover : textureCarryovers) {
            uint32_t physicalIndex = UINT32_MAX;
            for (uint32_t i = 0; i < physicalResources.Size(); i++) {
                if (physicalResources[i].image == carryover.physicalImage) {
                    physicalIndex = i;
                    break;
                }
            }

            if (physicalIndex == UINT32_MAX) {
                LOG_ERROR(Renderer, "Carryover texture '{}' physical resource not found", carryover.dstName.ToString());
                continue;
            }

            TextureResource* newTex = GetOrCreateTexture(carryover.dstName);
            newTex->textureInfo = carryover.textInfo;
            newTex->layout = carryover.layout;
            newTex->accumulatedUsage = carryover.accumulatedUsage;
            newTex->physicalIndex = physicalIndex;

            PhysicalResource& phys = physicalResources[physicalIndex];
            phys.logicalResourceIndices.PushBack(newTex->index);
            phys.usageChain.Clear();
            AppendUsageChain(phys, newTex->textureId, newTex->bCanUseAliasedTexture, bDebugLogging);
            phys.bCanAlias = false;
        }
        textureCarryovers.Clear();
    }

    //
    {
        ZoneScopedN("CarryoverBufferRestoration");
        for (auto& carryover : bufferCarryovers) {
            uint32_t physicalIndex = UINT32_MAX;
            for (uint32_t i = 0; i < physicalResources.Size(); i++) {
                if (physicalResources[i].buffer == carryover.buffer) {
                    physicalIndex = i;
                    break;
                }
            }

            if (physicalIndex == UINT32_MAX) {
                LOG_ERROR(Renderer, "Carryover buffer '{}' physical resource not found", carryover.dstName.ToString());
                continue;
            }

            BufferResource* newBuf = GetOrCreateBuffer(carryover.dstName);
            newBuf->bufferInfo = carryover.bufferInfo;
            newBuf->accumulatedUsage = carryover.accumulatedUsage;
            newBuf->physicalIndex = physicalIndex;

            PhysicalResource& phys = physicalResources[physicalIndex];
            phys.logicalResourceIndices.PushBack(newBuf->index);
            phys.usageChain.Clear();
            AppendUsageChain(phys, newBuf->bufferId, newBuf->bCanUseAliasedBuffer, bDebugLogging);
            phys.bCanAlias = false;
        }
        bufferCarryovers.Clear();
    }

    bDestroyViewportAssociated = false;
    bRemoveSwapchainPhysicals = false;
}

void RenderGraph::CreateTexture(const StringID textureId, const TextureInfo& texInfo, std::optional<VkClearValue> clearValue, bool bIsViewportScaled)
{
    TextureResource* tex = GetOrCreateTexture(textureId);

    if (tex->textureInfo.format != VK_FORMAT_UNDEFINED) {
        ENGINE_ASSERT(Renderer, tex->textureInfo.format == texInfo.format, "Texture format mismatch");
        ENGINE_ASSERT(Renderer, tex->textureInfo.width == texInfo.width, "Texture width mismatch");
        ENGINE_ASSERT(Renderer, tex->textureInfo.height == texInfo.height, "Texture height mismatch");
        ENGINE_ASSERT(Renderer, tex->textureInfo.mipLevels == texInfo.mipLevels, "Texture mip level mismatch");
    }

    ENGINE_ASSERT(Renderer, texInfo.format != VK_FORMAT_UNDEFINED, "Texture info uses undefined format");
    tex->textureInfo = texInfo;
    tex->bIsViewportScaled = bIsViewportScaled;
    tex->clear = clearValue;
}

void RenderGraph::AliasTexture(const StringID aliasId, const StringID existingId)
{
    uint32_t* idx = textureNameToIndex.Find(existingId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Aliasing texture failed because existing texture doesn't exist");
    textureNameToIndex[aliasId] = *idx;
}

void RenderGraph::AliasBuffer(const StringID aliasId, const StringID existingId)
{
    uint32_t* idx = bufferNameToIndex.Find(existingId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Aliasing buffer failed because existing buffer doesn't exist");
    bufferNameToIndex[aliasId] = *idx;
}

void RenderGraph::CreateBuffer(StringID bufferId, VkDeviceSize size, bool bIsViewportScaled, bool bCanAlias)
{
    BufferResource* buf = GetOrCreateBuffer(bufferId);

    if (buf->bufferInfo.size != 0) {
        ENGINE_ASSERT(Renderer, buf->bufferInfo.size == size, "Buffer size mismatch");
    }

    buf->bufferInfo.size = size;
    buf->bCanUseAliasedBuffer = bCanAlias;
    buf->bIsViewportScaled = bIsViewportScaled;
}

void RenderGraph::CreateBufferAligned(StringID bufferId, VkDeviceSize size, VkDeviceSize minAlignment, bool bIsViewportScaled, bool bCanAlias)
{
    BufferResource* buf = GetOrCreateBuffer(bufferId);

    if (buf->bufferInfo.size != 0) {
        ENGINE_ASSERT(Renderer, buf->bufferInfo.size == size, "Buffer size mismatch");
        ENGINE_ASSERT(Renderer, buf->minAlignment == minAlignment, "Buffer alignment mismatch");
    }

    buf->bufferInfo.size = size;
    buf->minAlignment = minAlignment;
    buf->bCanUseAliasedBuffer = bCanAlias;
    buf->bIsViewportScaled = bIsViewportScaled;
}

void RenderGraph::ImportTexture(StringID textureId,
                                VkImage image,
                                VkImageView view,
                                const TextureInfo& info,
                                VkImageUsageFlags usage,
                                VkImageLayout initialLayout,
                                VkPipelineStageFlags2 initialStage,
                                VkImageLayout finalLayout,
                                bool bIsSwapchain)
{
    TextureResource* tex = GetOrCreateTexture(textureId);
    tex->textureInfo = info;
    tex->accumulatedUsage = usage;

    if (!tex->HasPhysical()) {
        uint32_t foundIndex = UINT32_MAX;
        for (uint32_t i = 0; i < physicalResources.Size(); i++) {
            auto& phys = physicalResources[i];
            if (phys.bIsImported && phys.image == image) {
                foundIndex = i;
                ENGINE_ASSERT(Renderer, phys.dimensions.format == info.format, "Reimported image format mismatch");
                ENGINE_ASSERT(Renderer, phys.dimensions.width == info.width, "Reimported image width mismatch");
                ENGINE_ASSERT(Renderer, phys.dimensions.height == info.height, "Reimported image height mismatch");
                ENGINE_ASSERT(Renderer, phys.dimensions.levels == info.mipLevels, "Reimported image mip level mismatch");
                break;
            }
        }

        if (foundIndex != UINT32_MAX) {
            tex->physicalIndex = foundIndex;
        }
        else {
            tex->physicalIndex = physicalResources.Size();
            physicalResources.EmplaceBack();
            auto& phys = physicalResources[tex->physicalIndex];
            phys.image = image;
            phys.imageView = view;
            phys.mipViews[0] = view;
            phys.bIsImported = true;
            phys.bIsSwapchain = bIsSwapchain;

            phys.dimensions.type = ResourceDimensions::Type::Image;
            phys.dimensions.format = info.format;
            phys.dimensions.width = info.width;
            phys.dimensions.height = info.height;
            phys.dimensions.depth = 1;
            phys.dimensions.levels = info.mipLevels;
            phys.dimensions.layers = 1;
            phys.dimensions.samples = 1;
        }
    }

    auto& phys = physicalResources[tex->physicalIndex];
    tex->layout = initialLayout;
    phys.event.stages = initialStage;
    phys.event.access = VK_ACCESS_2_NONE;

    phys.aspect = VkHelpers::GetImageAspect(info.format);
    phys.dimensions.resourceId = textureId;
    phys.usageChain.Clear();
    tex->finalLayout = finalLayout;
}

void RenderGraph::ImportBufferNoBarrier(StringID bufferId, VkBuffer buffer, VkDeviceAddress address, const BufferInfo& info)
{
    BufferResource* buf = GetOrCreateBuffer(bufferId);
    buf->bufferInfo = info;
    buf->accumulatedUsage = info.usage;
    if (!buf->HasPhysical()) {
        uint32_t foundIndex = UINT32_MAX;
        for (uint32_t i = 0; i < physicalResources.Size(); i++) {
            auto& phys = physicalResources[i];
            if (phys.bIsImported && phys.buffer == buffer) {
                foundIndex = i;
                ENGINE_ASSERT(Renderer, phys.dimensions.bufferSize == info.size, "Reimported buffer size mismatch");
                ENGINE_ASSERT(Renderer, phys.dimensions.bufferUsage == info.usage, "Reimported buffer usage mismatch");
                ENGINE_ASSERT(Renderer, phys.bufferAddress == address, "Reimported buffer address mismatch");
                ENGINE_ASSERT(Renderer, phys.addressRetrieved, "Reimported buffer not marked as address retrieved");
                break;
            }
        }

        if (foundIndex != UINT32_MAX) {
            buf->physicalIndex = foundIndex;
        }
        else {
            buf->physicalIndex = physicalResources.Size();
            physicalResources.EmplaceBack();
            auto& phys = physicalResources[buf->physicalIndex];
            phys.buffer = buffer;
            phys.bufferAddress = address;
            phys.bIsImported = true;

            phys.dimensions.type = ResourceDimensions::Type::Buffer;
            phys.dimensions.bufferSize = info.size;
            phys.dimensions.bufferUsage = info.usage;
        }
    }

    auto& phys = physicalResources[buf->physicalIndex];
    phys.dimensions.resourceId = bufferId;
    phys.usageChain.Clear();
    phys.bDisableBarriers = true;
}

void RenderGraph::ImportBuffer(StringID bufferId, VkBuffer buffer, VkDeviceAddress address, const BufferInfo& info, PipelineEvent initialState)
{
    BufferResource* buf = GetOrCreateBuffer(bufferId);
    buf->bufferInfo = info;
    buf->accumulatedUsage = info.usage;
    if (!buf->HasPhysical()) {
        uint32_t foundIndex = UINT32_MAX;
        for (uint32_t i = 0; i < physicalResources.Size(); i++) {
            auto& phys = physicalResources[i];
            if (phys.bIsImported && phys.buffer == buffer) {
                foundIndex = i;
                ENGINE_ASSERT(Renderer, phys.dimensions.bufferSize == info.size, "Reimported buffer size mismatch");
                ENGINE_ASSERT(Renderer, phys.dimensions.bufferUsage == info.usage, "Reimported buffer usage mismatch");
                ENGINE_ASSERT(Renderer, phys.bufferAddress == address, "Reimported buffer address mismatch");
                ENGINE_ASSERT(Renderer, phys.addressRetrieved, "Reimported buffer not marked as address retrieved");
                break;
            }
        }

        if (foundIndex != UINT32_MAX) {
            buf->physicalIndex = foundIndex;
        }
        else {
            buf->physicalIndex = physicalResources.Size();
            physicalResources.EmplaceBack();
            auto& phys = physicalResources[buf->physicalIndex];
            phys.buffer = buffer;
            phys.bufferAddress = address;
            phys.bIsImported = true;

            phys.dimensions.type = ResourceDimensions::Type::Buffer;
            phys.dimensions.bufferSize = info.size;
            phys.dimensions.bufferUsage = info.usage;
        }
    }

    auto& phys = physicalResources[buf->physicalIndex];
    phys.event.stages = initialState.stages;
    phys.event.access = initialState.access;
    phys.dimensions.resourceId = bufferId;
    phys.usageChain.Clear();
    phys.bDisableBarriers = false;
}

bool RenderGraph::HasTexture(StringID textureId)
{
    return textureNameToIndex.Find(textureId) != nullptr;
}

bool RenderGraph::HasBuffer(StringID bufferId)
{
    return bufferNameToIndex.Find(bufferId) != nullptr;
}

VkImage RenderGraph::GetImageHandle(StringID textureId)
{
    uint32_t* idx = textureNameToIndex.Find(textureId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Texture not found");

    auto& tex = textures[*idx];
    ENGINE_ASSERT(Renderer, tex.HasPhysical(), "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].image;
}

VkImageView RenderGraph::GetImageViewHandle(StringID textureId)
{
    uint32_t* idx = textureNameToIndex.Find(textureId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Texture not found");

    auto& tex = textures[*idx];
    ENGINE_ASSERT(Renderer, tex.HasPhysical(), "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].imageView;
}

VkImageView RenderGraph::GetImageViewMipHandle(StringID textureId, uint32_t mipLevel)
{
    uint32_t* idx = textureNameToIndex.Find(textureId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Texture not found");
    ENGINE_ASSERT(Renderer, mipLevel < RDG_MAX_MIP_LEVELS, "Mip level out of range");

    auto& tex = textures[*idx];
    ENGINE_ASSERT(Renderer, tex.HasPhysical(), "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].mipViews[mipLevel];
}

VkImageView RenderGraph::GetDepthOnlyImageViewHandle(StringID textureId)
{
    uint32_t* idx = textureNameToIndex.Find(textureId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Texture not found");

    auto& tex = textures[*idx];
    ENGINE_ASSERT(Renderer, tex.HasPhysical(), "Texture has no physical resource");

    auto& phys = physicalResources[tex.physicalIndex];

    if (phys.aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
        return phys.imageView;
    }

    ENGINE_ASSERT(Renderer, phys.depthOnlyView != VK_NULL_HANDLE, "Texture has no depth only view");
    return phys.depthOnlyView;
}

VkImageView RenderGraph::GetStencilOnlyImageViewHandle(StringID textureId)
{
    uint32_t* idx = textureNameToIndex.Find(textureId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Texture not found");

    auto& tex = textures[*idx];
    ENGINE_ASSERT(Renderer, tex.HasPhysical(), "Texture has no physical resource");

    auto& phys = physicalResources[tex.physicalIndex];

    if (phys.aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
        return phys.imageView;
    }

    ENGINE_ASSERT(Renderer, phys.stencilOnlyView != VK_NULL_HANDLE, "Texture has no stencil only view");
    return phys.stencilOnlyView;
}

const ResourceDimensions& RenderGraph::GetImageDimensions(StringID textureId)
{
    uint32_t* idx = textureNameToIndex.Find(textureId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Texture not found");

    auto& tex = textures[*idx];
    ENGINE_ASSERT(Renderer, tex.HasPhysical(), "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].dimensions;
}

const VkImageAspectFlags RenderGraph::GetImageAspect(StringID textureId)
{
    uint32_t* idx = textureNameToIndex.Find(textureId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Texture not found");

    auto& tex = textures[*idx];
    ENGINE_ASSERT(Renderer, tex.HasPhysical(), "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].aspect;
}

uint32_t RenderGraph::GetSampledImageViewDescriptorIndex(StringID textureId)
{
    uint32_t* idx = textureNameToIndex.Find(textureId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Texture not found");

    auto& tex = textures[*idx];
    ENGINE_ASSERT(Renderer, tex.HasPhysical(), "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].sampledDescriptorHandle.index;
}

uint32_t RenderGraph::GetStorageImageViewDescriptorIndex(StringID textureId, uint32_t mipLevel)
{
    uint32_t* idx = textureNameToIndex.Find(textureId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Texture not found");

    auto& tex = textures[*idx];
    ENGINE_ASSERT(Renderer, tex.HasPhysical(), "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].storageMipDescriptorHandles[mipLevel].index;
}

uint32_t RenderGraph::GetDepthOnlySampledImageViewDescriptorIndex(StringID textureId)
{
    uint32_t* idx = textureNameToIndex.Find(textureId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Texture not found");

    auto& tex = textures[*idx];
    ENGINE_ASSERT(Renderer, tex.HasPhysical(), "Texture has no physical resource");
    auto& phys = physicalResources[tex.physicalIndex];

    if (phys.aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
        return phys.sampledDescriptorHandle.index;
    }

    ENGINE_ASSERT(Renderer, phys.depthOnlyDescriptorHandle.IsValid(), "Texture has no depth only descriptor");
    return phys.depthOnlyDescriptorHandle.index;
}

uint32_t RenderGraph::GetStencilOnlyStorageImageViewDescriptorIndex(StringID textureId)
{
    uint32_t* idx = textureNameToIndex.Find(textureId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Texture not found");

    auto& tex = textures[*idx];
    ENGINE_ASSERT(Renderer, tex.HasPhysical(), "Texture has no physical resource");
    auto& phys = physicalResources[tex.physicalIndex];

    if (phys.aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
        return phys.sampledDescriptorHandle.index;
    }

    ENGINE_ASSERT(Renderer, phys.stencilOnlyDescriptorHandle.IsValid(), "Texture has no stencil only descriptor");
    return phys.stencilOnlyDescriptorHandle.index;
}

VkBuffer RenderGraph::GetBufferHandle(StringID bufferId)
{
    uint32_t* idx = bufferNameToIndex.Find(bufferId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Buffer not found");

    auto& buf = buffers[*idx];
    ENGINE_ASSERT(Renderer, buf.HasPhysical(), "Buffer has no physical resource");

    return physicalResources[buf.physicalIndex].buffer;
}

VkDeviceAddress RenderGraph::GetBufferAddress(StringID bufferId)
{
    uint32_t* idx = bufferNameToIndex.Find(bufferId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Buffer not found");

    auto& buf = buffers[*idx];
    ENGINE_ASSERT(Renderer, buf.HasPhysical(), "Buffer has no physical resource");

    auto& phys = physicalResources[buf.physicalIndex];

    if (!phys.addressRetrieved) {
        phys.bufferAddress = allocFns.getBufferDeviceAddress(context, phys.buffer);
        phys.addressRetrieved = true;
    }

    return phys.bufferAddress;
}

VkDeviceAddress RenderGraph::TryGetBufferAddress(StringID bufferId)
{
    uint32_t* idx = bufferNameToIndex.Find(bufferId);
    if (idx == nullptr) { return 0; }

    auto& buf = buffers[*idx];
    if (!buf.HasPhysical()) { return 0; }

    auto& phys = physicalResources[buf.physicalIndex];

    if (!phys.addressRetrieved) {
        phys.bufferAddress = allocFns.getBufferDeviceAddress(context, phys.buffer);
        phys.addressRetrieved = true;
    }

    return phys.bufferAddress;
}

PipelineEvent RenderGraph::GetBufferState(StringID bufferId)
{
    uint32_t* idx = bufferNameToIndex.Find(bufferId);
    ENGINE_ASSERT(Renderer, idx != nullptr, "Buffer not found");

    auto& buf = buffers[*idx];
    ENGINE_ASSERT(Renderer, buf.HasPhysical(), "Buffer has no physical resource");

    return physicalResources[buf.physicalIndex].event;
}

void RenderGraph::CarryTextureToNextFrame(StringID textureId, StringID newTextureId, VkImageUsageFlags additionalUsage)
{
    TextureResource* tex = GetOrCreateTexture(textureId);
    tex->bCanUseAliasedTexture = false;
    tex->accumulatedUsage |= additionalUsage;

    if (tex->physicalIndex != UINT32_MAX) {
        auto& phys = physicalResources[tex->physicalIndex];
        if (phys.IsAllocated()) {
            ENGINE_ASSERT(Renderer, (phys.dimensions.bufferUsage & additionalUsage) == additionalUsage, "Existing physical texture usage is not a superset of required usage");
        }
    }

    for (const auto& c : textureCarryovers) {
        ENGINE_ASSERT(Renderer, c.srcName != textureId, "Source texture already designated for carryover");
        ENGINE_ASSERT(Renderer, c.dstName != newTextureId, "Destination texture name already used in another carryover");
        if (const TextureResource* otherTex = GetTexture(c.srcName)) {
            ENGINE_ASSERT(Renderer, otherTex->index != tex->index, "Cannot carry over texture already marked to be carried over");
        }
    }

    textureCarryovers.PushBack(TextureFrameCarryover{textureId, newTextureId});
}

void RenderGraph::CarryBufferToNextFrame(StringID bufferId, StringID newBufferId, VkBufferUsageFlags additionalUsage)
{
    BufferResource* buf = GetOrCreateBuffer(bufferId);
    buf->bCanUseAliasedBuffer = false;
    buf->accumulatedUsage |= additionalUsage;

    if (buf->physicalIndex != UINT32_MAX) {
        auto& phys = physicalResources[buf->physicalIndex];
        if (phys.IsAllocated()) {
            ENGINE_ASSERT(Renderer, (phys.dimensions.bufferUsage & additionalUsage) == additionalUsage, "Existing physical buffer usage is not a superset of required usage");
        }
    }

    for (const auto& c : bufferCarryovers) {
        ENGINE_ASSERT(Renderer, c.srcName != bufferId, "Source buffer already designated for carryover");
        ENGINE_ASSERT(Renderer, c.dstName != newBufferId, "Destination buffer name already used in another carryover");
    }

    bufferCarryovers.PushBack(BufferFrameCarryover{bufferId, newBufferId});
}

UploadAllocation RenderGraph::AllocateTransient(size_t size)
{
    TransientUploadArena& arena = uploadArenas[currentFrameIndex];
    size_t offset = arena.allocator.Allocate(size);

    if (offset == SIZE_MAX) {
        size_t required = arena.allocator.GetUsed() + size;
        size_t newSize = std::max(arena.size * 2, required);
        RecreateTransientArena(currentFrameIndex, newSize);
        offset = arena.allocator.Allocate(size);
        ENGINE_ASSERT(Renderer, offset != SIZE_MAX, "Still OOM after transient arena resize");
    }

    return {
        .ptr = static_cast<char*>(arena.mappedData) + offset,
        .address = arena.address + offset,
        .offset = offset
    };
}

void RenderGraph::RecreateTransientArena(uint32_t frameIndex, size_t newSize)
{
    TransientUploadArena& arena = uploadArenas[frameIndex];
    Core::LinearAllocator newAllocator = Core::LinearAllocator::CreateExpanded(arena.allocator, newSize);

    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.usage = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;
    bufferInfo.size = newSize;
    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    auto newAlloc = allocFns.createBuffer(context, bufferInfo, vmaAllocInfo); {
        auto debugName = Core::InlineString<>("rdgFrameBufferUploader_");
        debugName.Append(frameIndex);
        allocFns.setDebugName(context, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(newAlloc.buffer), debugName.c_str());
    }

    memcpy(newAlloc.mappedData, arena.mappedData, arena.allocator.GetUsed());
    allocFns.destroyBuffer(context, arena.buffer, arena.bufferAllocation);

    arena.buffer = newAlloc.buffer;
    arena.bufferAllocation = newAlloc.allocation;
    arena.mappedData = newAlloc.mappedData;
    arena.allocator = newAllocator;
    arena.size = newSize;
}

void RenderGraph::RegisterPersistentBuffer(StringID name, VkBufferUsageFlags usage, Core::InlineFunction<void(uint64_t), 32> onDestroyUserData)
{
    for (const auto& entry : persistentBuffers) {
        if (entry.name == name) { return; }
    }
    PersistentBufferSlots& entry = persistentBuffers.EmplaceBack();
    entry.name = name;
    entry.usage = usage;
    entry.onDestroyUserData = std::move(onDestroyUserData);
}

bool RenderGraph::EnsurePersistentBufferCapacity(StringID name, VkDeviceSize requiredSize)
{
    for (auto& entry : persistentBuffers) {
        if (entry.name != name) { continue; }

        PersistentBuffer& slot = entry.slots[currentFrameIndex];
        if (requiredSize <= slot.capacity) { return false; }

        if (slot.buffer != VK_NULL_HANDLE) {
            if (slot.userData != 0 && entry.onDestroyUserData) { entry.onDestroyUserData(slot.userData); }
            allocFns.destroyBuffer(context, slot.buffer, slot.allocation);
            slot = {};
        }

        VkBufferCreateInfo bufferInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = requiredSize;
        bufferInfo.usage = entry.usage;
        VmaAllocationCreateInfo vmaInfo{};
        vmaInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        auto _alloc = allocFns.createBuffer(context, bufferInfo, vmaInfo);
        slot.buffer = _alloc.buffer;
        slot.allocation = _alloc.allocation;
        slot.capacity = requiredSize;

        VkBufferDeviceAddressInfo addrInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = slot.buffer};
        slot.address = vkGetBufferDeviceAddress(context->device, &addrInfo);
        slot.userData = 0;
        return true;
    }
    ENGINE_ASSERT(Renderer, false, "EnsurePersistentBufferCapacity: buffer '{}' not registered", name.ToString());
    return false;
}

PersistentBuffer& RenderGraph::GetPersistentBuffer(StringID name)
{
    for (auto& entry : persistentBuffers) {
        if (entry.name == name) { return entry.slots[currentFrameIndex]; }
    }
    ENGINE_ASSERT(Renderer, false, "GetPersistentBuffer: buffer '{}' not registered", name.ToString());
    static PersistentBuffer dummy{};
    return dummy;
}

void RenderGraph::ImportPersistentBuffer(StringID name)
{
    for (const auto& entry : persistentBuffers) {
        if (entry.name != name) { continue; }
        const PersistentBuffer& slot = entry.slots[currentFrameIndex];
        if (slot.buffer == VK_NULL_HANDLE) {
            LOG_WARN(Renderer, "ImportPersistentBuffer: Attempted to import a persistent buffer that does not exist.");
            return;
        }
        const BufferInfo info{.size = slot.capacity, .usage = entry.usage};
        ImportBuffer(name, slot.buffer, slot.address, info, slot.lastState);
        return;
    }
    ENGINE_ASSERT(Renderer, false, "ImportPersistentBuffer: buffer '{}' not registered", name.ToString());
}

void RenderGraph::WriteAccelerationStructureDescriptor(StringID name, VkAccelerationStructureKHR handle)
{
    PersistentBuffer& buf = GetPersistentBuffer(name);
    buf.userData = reinterpret_cast<uint64_t>(handle);
    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addrInfo.accelerationStructure = handle;
    const VkDeviceAddress address = vkGetAccelerationStructureDeviceAddressKHR(context->device, &addrInfo);
    resourceManager->bindlessRDGRTDescriptorBuffer.WriteAccelerationStructureDescriptor(currentFrameIndex, address);
    buf.userData2 = currentFrameIndex;
}

uint32_t RenderGraph::GetAccelerationStructureDescriptorIndex(StringID name)
{
    return static_cast<uint32_t>(GetPersistentBuffer(name).userData2);
}

void RenderGraph::ExportGraphviz()
{
    auto StageColor = [](VkPipelineStageFlags2 stages) -> const char* {
        if (stages & VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT) { return "#f8cecc"; }
        if (stages & (VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT)) { return "#fff2cc"; }
        if (stages & (VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                      VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT)) { return "#dae8fc"; }
        if (stages & VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT) { return "#d5e8d4"; }
        return "#f5f5f5";
    };

    Core::ArenaString dot(arena, 65536);
    dot.Append("digraph RDG {\n");
    dot.Append("    rankdir=TB;\n");
    dot.Append("    splines=ortho;\n");
    dot.Append("    nodesep=0.5;\n");
    dot.Append("    ranksep=1.0;\n");
    dot.Append("    node [shape=box fontname=\"Consolas\" style=filled];\n\n");

    // Legend
    dot.Append("    subgraph cluster_legend {\n");
    dot.Append("        label=\"Legend\"; fontname=\"Consolas\"; style=filled; fillcolor=\"#ffffff\";\n");
    dot.Append("        legend_graphics  [label=\"Graphics\"     fillcolor=\"#dae8fc\"];\n");
    dot.Append("        legend_compute   [label=\"Compute\"      fillcolor=\"#d5e8d4\"];\n");
    dot.Append("        legend_transfer  [label=\"Transfer/Copy/Clear\" fillcolor=\"#fff2cc\"];\n");
    dot.Append("        legend_heavy     [label=\"All Graphics (heavy)\" fillcolor=\"#f8cecc\"];\n");
    dot.Append("        legend_unknown   [label=\"Unknown\"      fillcolor=\"#f5f5f5\"];\n");
    dot.Append("        legend_graphics -> legend_compute -> legend_transfer -> legend_heavy -> legend_unknown [style=invis];\n");
    dot.Append("    }\n\n");

    // Node declarations with wave label and stage color
    for (const RenderPass* pass : sortedPasses) {
        dot.Append("    \"");
        dot.Append(pass->renderPassId.ToString());
        dot.Append("\" [label=\"");
        dot.Append(pass->renderPassId.ToString());
        dot.Append("\\nwave ");
        dot.Append(pass->waveIndex);
        dot.Append("\" fillcolor=\"");
        dot.Append(StageColor(pass->stages));
        dot.Append("\"];\n");
    }

    dot.Append("\n");

    // One subgraph per wave so Graphviz ranks them on the same row
    for (uint32_t w = 0; w + 1 < waveOffsets.Size(); w++) {
        const uint32_t start = waveOffsets[w];
        const uint32_t end = waveOffsets[w + 1];

        dot.Append("    { rank=same;");
        for (uint32_t i = start; i < end; i++) {
            dot.Append(" \"");
            dot.Append(sortedPasses[i]->renderPassId.ToString());
            dot.Append("\";");
        }
        dot.Append(" }\n");
    }

    dot.Append("\n");

    // One edge per unique src->dst pair; outEdges may contain duplicates from multiple shared resources
    for (const RenderPass* pass : sortedPasses) {
        uint32_t lastEmitted = UINT32_MAX;
        for (const uint32_t neighborIdx : pass->outEdges) {
            if (neighborIdx == lastEmitted) { continue; }
            lastEmitted = neighborIdx;
            const RenderPass* neighbor = passes[neighborIdx];
            dot.Append("    \"");
            dot.Append(pass->renderPassId.ToString());
            dot.Append("\" -> \"");
            dot.Append(neighbor->renderPassId.ToString());
            dot.Append("\";\n");
        }
    }

    dot.Append("}\n");

    const Core::Path outPath = Platform::GetAssetPath() / "visualizations" / "rdg.dot";
    Platform::WriteFile(outPath, dot.c_str(), dot.Size());
}

void RenderGraph::LogImageBarrier(StringID textureId, const VkImageMemoryBarrier2& barrier, uint32_t physicalIndex) const
{
    if (!bDebugLogging) return;

    auto LayoutToString = [](VkImageLayout layout) -> const char* {
        switch (layout) {
            case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
            case VK_IMAGE_LAYOUT_GENERAL: return "GENERAL";
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return "TRANSFER_SRC";
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return "TRANSFER_DST";
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return "SHADER_READ_ONLY";
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return "COLOR_ATTACHMENT";
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return "DEPTH_STENCIL_ATTACHMENT";
            case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL: return "DEPTH_ATTACHMENT";
            case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL: return "STENCIL_ATTACHMENT";
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL: return "DEPTH_STENCIL_READ_ONLY";
            case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL: return "DEPTH_READ_ONLY";
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return "PRESENT_SRC";
            default: return "UNKNOWN";
        }
    };

    LOG_INFO(Renderer, "  [BARRIER] {} ({}): {} -> {}", textureId.ToString(), physicalIndex, LayoutToString(barrier.oldLayout), LayoutToString(barrier.newLayout));
}

void RenderGraph::LogBufferBarrier(StringID bufferId, VkAccessFlags2 access) const
{
    if (!bDebugLogging) return;

    const char* accessType = "read";
    if (access & (VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT)) {
        accessType = "write";
    }

    LOG_INFO(Renderer, "  [BUFFER BARRIER] {} ({})", bufferId.ToString(), accessType);
}


TextureResource* RenderGraph::GetTexture(StringID imageId)
{
    if (uint32_t* idx = textureNameToIndex.Find(imageId)) {
        return &textures[*idx];
    }
    return nullptr;
}

TextureResource* RenderGraph::GetOrCreateTexture(StringID textureId)
{
    if (uint32_t* idx = textureNameToIndex.Find(textureId)) {
        return &textures[*idx];
    }

    uint32_t index = textures.Size();
    textures.PushBack(TextureResource{
        .textureId = textureId,
        .index = index,
    });
    textureNameToIndex[textureId] = index;

    return &textures[index];
}

BufferResource* RenderGraph::GetBuffer(const StringID bufferId)
{
    if (uint32_t* idx = bufferNameToIndex.Find(bufferId)) {
        return &buffers[*idx];
    }
    return nullptr;
}

BufferResource* RenderGraph::GetOrCreateBuffer(StringID bufferId)
{
    if (uint32_t* idx = bufferNameToIndex.Find(bufferId)) {
        return &buffers[*idx];
    }

    uint32_t index = buffers.Size();
    buffers.PushBack(BufferResource{
        .bufferId = bufferId,
        .index = index,
    });
    bufferNameToIndex[bufferId] = index;

    return &buffers[index];
}

RenderGraphAllocFns::ImageAlloc RenderGraphAllocFns::DefaultCreateImage(const VulkanContext* context, const VkImageCreateInfo& imageInfo)
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK(vmaCreateImage(context->allocator, &imageInfo, &allocInfo, &image, &alloc, nullptr));
    return {image, alloc};
}

VkImageView RenderGraphAllocFns::DefaultCreateImageView(const VulkanContext* context, const VkImageViewCreateInfo& info)
{
    VkImageView view = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(context->device, &info, nullptr, &view));
    return view;
}

void RenderGraphAllocFns::DefaultDestroyImage(const VulkanContext* context, VkImage image, VmaAllocation allocation)
{
    vmaDestroyImage(context->allocator, image, allocation);
}

void RenderGraphAllocFns::DefaultDestroyImageView(const VulkanContext* context, VkImageView view)
{
    vkDestroyImageView(context->device, view, nullptr);
}

RenderGraphAllocFns::BufferAlloc RenderGraphAllocFns::DefaultCreateBuffer(const VulkanContext* context, const VkBufferCreateInfo& bufferInfo, const VmaAllocationCreateInfo& vmaAllocInfo)
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo;
    VK_CHECK(vmaCreateBuffer(context->allocator, &bufferInfo, &vmaAllocInfo, &buffer, &alloc, &allocInfo));
    return {buffer, alloc, allocInfo.pMappedData};
}

RenderGraphAllocFns::BufferAlloc RenderGraphAllocFns::DefaultCreateBufferAligned(const VulkanContext* context, const VkBufferCreateInfo& bufferInfo, const VmaAllocationCreateInfo& vmaAllocInfo, VkDeviceSize minAlignment)
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo;
    VK_CHECK(vmaCreateBufferWithAlignment(context->allocator, &bufferInfo, &vmaAllocInfo, minAlignment, &buffer, &alloc, &allocInfo));
    return {buffer, alloc, allocInfo.pMappedData};
}

void RenderGraphAllocFns::DefaultDestroyBuffer(const VulkanContext* context, VkBuffer buffer, VmaAllocation allocation)
{
    vmaDestroyBuffer(context->allocator, buffer, allocation);
}

VkDeviceAddress RenderGraphAllocFns::DefaultGetBufferDeviceAddress(const VulkanContext* context, VkBuffer buffer)
{
    VkBufferDeviceAddressInfo info = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = buffer;
    return vkGetBufferDeviceAddress(context->device, &info);
}

void RenderGraphAllocFns::DefaultSetDebugName(const VulkanContext* context, VkObjectType type, uint64_t handle, const char* name)
{
#ifdef ENABLE_VULKAN_VALIDATION
    VkDebugUtilsObjectNameInfoEXT nameInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    nameInfo.objectType = type;
    nameInfo.objectHandle = handle;
    nameInfo.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(context->device, &nameInfo);
#endif
}

void RenderGraphAllocFns::DefaultCmdPipelineBarrier2(VkCommandBuffer cmd, const VkDependencyInfo* dependencyInfo)
{
    vkCmdPipelineBarrier2(cmd, dependencyInfo);
}

void RenderGraphAllocFns::DefaultCmdClearColorImage(VkCommandBuffer cmd, VkImage image, VkImageLayout layout, const VkClearColorValue* color, uint32_t rangeCount, const VkImageSubresourceRange* ranges)
{
    vkCmdClearColorImage(cmd, image, layout, color, rangeCount, ranges);
}

void RenderGraphAllocFns::DefaultCmdClearDepthStencilImage(VkCommandBuffer cmd, VkImage image, VkImageLayout layout, const VkClearDepthStencilValue* depthStencil, uint32_t rangeCount, const VkImageSubresourceRange* ranges)
{
    vkCmdClearDepthStencilImage(cmd, image, layout, depthStencil, rangeCount, ranges);
}

void RenderGraphAllocFns::DefaultCmdBeginDebugUtilsLabel(VkCommandBuffer cmd, const VkDebugUtilsLabelEXT* label)
{
    vkCmdBeginDebugUtilsLabelEXT(cmd, label);
}

void RenderGraphAllocFns::DefaultCmdEndDebugUtilsLabel(VkCommandBuffer cmd)
{
    vkCmdEndDebugUtilsLabelEXT(cmd);
}

void RenderGraph::DestroyPhysicalResource(PhysicalResource& resource)
{
    if (resource.bIsImported) {
        return;
    }

    if (resource.dimensions.IsImage()) {
        for (uint32_t mip = 0; mip < resource.dimensions.levels; ++mip) {
            if (resource.mipViews[mip] != VK_NULL_HANDLE) {
                allocFns.destroyImageView(context, resource.mipViews[mip]);
                resource.mipViews[mip] = VK_NULL_HANDLE;
            }
            if (resource.storageMipDescriptorHandles[mip].IsValid()) {
                ImageChannelType storageType = GetImageChannelType(resource.dimensions.format, resource.aspect);
                switch (storageType) {
                    case ImageChannelType::Float4:
                        transientStorageFloat4HandleAllocator.Remove(resource.storageMipDescriptorHandles[mip]);
                        break;
                    case ImageChannelType::Float2:
                        transientStorageFloat2HandleAllocator.Remove(resource.storageMipDescriptorHandles[mip]);
                        break;
                    case ImageChannelType::Float:
                        transientStorageFloatHandleAllocator.Remove(resource.storageMipDescriptorHandles[mip]);
                        break;
                    case ImageChannelType::UInt4:
                        transientStorageUInt4HandleAllocator.Remove(resource.storageMipDescriptorHandles[mip]);
                        break;
                    case ImageChannelType::UInt2:
                        transientStorageUInt2HandleAllocator.Remove(resource.storageMipDescriptorHandles[mip]);
                        break;
                    case ImageChannelType::UInt:
                        transientStorageUIntHandleAllocator.Remove(resource.storageMipDescriptorHandles[mip]);
                        break;
                }
                resource.storageMipDescriptorHandles[mip] = {};
            }
        }

        if (resource.imageView != VK_NULL_HANDLE) {
            allocFns.destroyImageView(context, resource.imageView);
            resource.imageView = VK_NULL_HANDLE;
        }
        if (resource.sampledDescriptorHandle.IsValid()) {
            ImageChannelType sampledChannelType = GetImageChannelType(resource.dimensions.format, resource.aspect);
            switch (sampledChannelType) {
                case ImageChannelType::Float4:
                    transientSampledImageHandleAllocator.Remove(resource.sampledDescriptorHandle);
                    break;
                case ImageChannelType::Float2:
                    transientSampledFloat2HandleAllocator.Remove(resource.sampledDescriptorHandle);
                    break;
                case ImageChannelType::Float:
                    transientSampledFloatHandleAllocator.Remove(resource.sampledDescriptorHandle);
                    break;
                case ImageChannelType::UInt4:
                    transientSampledUInt4HandleAllocator.Remove(resource.sampledDescriptorHandle);
                    break;
                case ImageChannelType::UInt2:
                    transientSampledUInt2HandleAllocator.Remove(resource.sampledDescriptorHandle);
                    break;
                case ImageChannelType::UInt:
                    transientSampledUIntHandleAllocator.Remove(resource.sampledDescriptorHandle);
                    break;
            }
            resource.sampledDescriptorHandle = {};
        }

        if (resource.depthOnlyView != VK_NULL_HANDLE) {
            allocFns.destroyImageView(context, resource.depthOnlyView);
            resource.depthOnlyView = VK_NULL_HANDLE;
        }
        if (resource.depthOnlyDescriptorHandle.IsValid()) {
            transientSampledFloatHandleAllocator.Remove(resource.depthOnlyDescriptorHandle);
            resource.depthOnlyDescriptorHandle = {};
        }

        if (resource.stencilOnlyView != VK_NULL_HANDLE) {
            allocFns.destroyImageView(context, resource.stencilOnlyView);
            resource.stencilOnlyView = VK_NULL_HANDLE;
        }
        if (resource.stencilOnlyDescriptorHandle.IsValid()) {
            transientStorageUIntHandleAllocator.Remove(resource.stencilOnlyDescriptorHandle);
            resource.stencilOnlyDescriptorHandle = {};
        }

        if (resource.image != VK_NULL_HANDLE) {
            allocFns.destroyImage(context, resource.image, resource.imageAllocation);
            resource.image = VK_NULL_HANDLE;
            resource.imageAllocation = VK_NULL_HANDLE;
        }
    }
    else {
        if (resource.buffer != VK_NULL_HANDLE) {
            allocFns.destroyBuffer(context, resource.buffer, resource.bufferAllocation);
            resource.buffer = VK_NULL_HANDLE;
            resource.bufferAllocation = VK_NULL_HANDLE;
        }
    }

    resource.descriptorWritten = false;
    resource.addressRetrieved = false;
    resource.event = {};
}

void RenderGraph::CreatePhysicalImage(PhysicalResource& resource, const ResourceDimensions& dim)
{
    auto debugName = Core::InlineString<>("PhysicalImage");
    debugName.Append(debugNameCounter++);
    resource.debugName = debugName;
    VkImageCreateInfo imageInfo = VkHelpers::ImageCreateInfo(dim.format, {dim.width, dim.height, dim.depth}, dim.imageUsage);
    imageInfo.mipLevels = dim.levels;
    imageInfo.arrayLayers = dim.layers;
    imageInfo.samples = static_cast<VkSampleCountFlagBits>(dim.samples);

    auto imageAlloc = allocFns.createImage(context, imageInfo);
    resource.image = imageAlloc.image;
    resource.imageAllocation = imageAlloc.allocation;

    allocFns.setDebugName(context, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(resource.image), resource.debugName.c_str());

    VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    if (dim.format == VK_FORMAT_D16_UNORM || dim.format == VK_FORMAT_D32_SFLOAT || dim.format == VK_FORMAT_X8_D24_UNORM_PACK32) {
        aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    else if (dim.format == VK_FORMAT_D16_UNORM_S8_UINT || dim.format == VK_FORMAT_D24_UNORM_S8_UINT || dim.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    else if (dim.format == VK_FORMAT_S8_UINT) {
        aspectFlags = VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    resource.aspect = aspectFlags;
    resource.dimensions = dim;
    resource.event = {};

    constexpr VkImageUsageFlags TRANSFER_ONLY = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((dim.imageUsage & ~TRANSFER_ONLY) == 0) {
        return;
    }

    VkImageViewCreateInfo viewInfo = VkHelpers::ImageViewCreateInfo(resource.image, dim.format, aspectFlags);

    VkImageViewCreateInfo sampledViewInfo = viewInfo;
    sampledViewInfo.subresourceRange.levelCount = dim.levels;
    resource.imageView = allocFns.createImageView(context, sampledViewInfo);

    if ((aspectFlags & VK_IMAGE_ASPECT_DEPTH_BIT) && (aspectFlags & VK_IMAGE_ASPECT_STENCIL_BIT)) {
        VkImageViewCreateInfo depthViewInfo = viewInfo;
        depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthViewInfo.subresourceRange.levelCount = dim.levels;
        resource.depthOnlyView = allocFns.createImageView(context, depthViewInfo);

        VkImageViewCreateInfo stencilViewInfo = viewInfo;
        stencilViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        stencilViewInfo.subresourceRange.levelCount = dim.levels;
        resource.stencilOnlyView = allocFns.createImageView(context, stencilViewInfo);
    }

    for (uint32_t mip = 0; mip < dim.levels; ++mip) {
        VkImageViewCreateInfo mipViewInfo = viewInfo;
        mipViewInfo.subresourceRange.baseMipLevel = mip;
        mipViewInfo.subresourceRange.levelCount = 1;

        // For depth+stencil, mipViews are depth-only (stencil mips not supported)
        if ((aspectFlags & VK_IMAGE_ASPECT_DEPTH_BIT) && (aspectFlags & VK_IMAGE_ASPECT_STENCIL_BIT)) {
            mipViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        }

        resource.mipViews[mip] = allocFns.createImageView(context, mipViewInfo);
    }
}

void RenderGraph::CreatePhysicalBuffer(PhysicalResource& resource, const ResourceDimensions& dim)
{
    auto debugName = Core::InlineString<>("PhysicalBuffer");
    debugName.Append(debugNameCounter++);
    resource.debugName = debugName;
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dim.bufferSize;
    bufferInfo.usage = dim.bufferUsage;

    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    const auto bufAlloc = dim.bufferMinAlignment > 0
                              ? allocFns.createBufferAligned(context, bufferInfo, vmaAllocInfo, dim.bufferMinAlignment)
                              : allocFns.createBuffer(context, bufferInfo, vmaAllocInfo);
    resource.buffer = bufAlloc.buffer;
    resource.bufferAllocation = bufAlloc.allocation;

    allocFns.setDebugName(context, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(resource.buffer), resource.debugName.c_str());

    resource.dimensions = dim;
    resource.event = {};
}

VRAMReport RenderGraph::GenerateVramReport() const
{
    VRAMReport report;

    // Logical VRAM (no aliasing)
    // Sum declared sizes per category. Resources with multiple category bits are counted in each.
    auto AddLogicalBytes = [&](ResourceCategory cat, VkDeviceSize bytes) {
        const auto mask = static_cast<uint64_t>(cat);
        for (uint32_t bit = 0; bit < RESOURCE_CATEGORY_BIT_COUNT; ++bit) {
            if (mask & (1ull << bit)) {
                report.logical[bit] += bytes;
            }
        }
    };

    for (const auto& tex : textures) {
        if (tex.accumulatedUsage == 0) { continue; }
        const VkDeviceSize rowBytes = static_cast<VkDeviceSize>(tex.textureInfo.width) * tex.textureInfo.height * 4;
        AddLogicalBytes(tex.category, rowBytes);
        report.logicalTotal += rowBytes;
    }
    for (const auto& buf : buffers) {
        if (buf.accumulatedUsage == 0) { continue; }
        AddLogicalBytes(buf.category, buf.bufferInfo.size);
        report.logicalTotal += buf.bufferInfo.size;
    }

    // Physical VRAM Usage (w/ aliasing)
    for (const auto& phys : physicalResources) {
        if (!phys.IsAllocated()) { continue; }

        VkDeviceSize size = 0;
        if (phys.dimensions.IsImage() && phys.imageAllocation != VK_NULL_HANDLE) {
            VmaAllocationInfo info{};
            vmaGetAllocationInfo(context->allocator, phys.imageAllocation, &info);
            size = info.size;
        }
        else if (phys.dimensions.IsBuffer() && phys.bufferAllocation != VK_NULL_HANDLE) {
            VmaAllocationInfo info{};
            vmaGetAllocationInfo(context->allocator, phys.bufferAllocation, &info);
            size = info.size;
        }

        if (size == 0) { continue; }
        report.physicalTotal += size;

        const auto mask = static_cast<uint64_t>(phys.category);
        const auto popCount = static_cast<uint32_t>(std::popcount(mask));

        if (popCount == 1) {
            for (uint32_t bit = 0; bit < RESOURCE_CATEGORY_BIT_COUNT; ++bit) {
                if (mask & (1ull << bit)) {
                    report.physicalExclusive[bit] += size;
                    break;
                }
            }
        }
        else if (popCount > 1) {
            report.physicalSharedPoolBytes += size;
            report.sharedPoolCategories |= phys.category;
        }
    }

    return report;
}

void RenderGraph::AppendUsageChain(PhysicalResource& phys, StringID resourceId, bool bCanAlias, bool debugLogging)
{
#ifdef WDEBUG
    if (!debugLogging) return;

    const char* name = resourceId.ToString();
    const char* nameStr = name ? name : "<unresolved>";

    if (!phys.usageChain.IsEmpty()) { phys.usageChain.Append(" -> "); }
    if (!bCanAlias) { phys.usageChain.Append("[noalias]"); }
    phys.usageChain.Append(nameStr);
#endif
}
} // Render
