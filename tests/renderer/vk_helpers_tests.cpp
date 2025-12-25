//
// Created by William on 2025-12-25.
//
// Tests for Vulkan helper functions that create commonly used Vulkan structures.
//

#include <catch2/catch_test_macros.hpp>

#include "render/vulkan/vk_helpers.h"

using namespace Render::VkHelpers;

TEST_CASE("GetAlignedSize aligns correctly", "[renderer][vk-helpers]") {
    SECTION("Already aligned value") {
        REQUIRE(GetAlignedSize(256, 256) == 256);
        REQUIRE(GetAlignedSize(512, 256) == 512);
        REQUIRE(GetAlignedSize(1024, 256) == 1024);
    }

    SECTION("Rounds up to alignment") {
        REQUIRE(GetAlignedSize(1, 256) == 256);
        REQUIRE(GetAlignedSize(257, 256) == 512);
        REQUIRE(GetAlignedSize(513, 256) == 768);
    }

    SECTION("Small alignments") {
        REQUIRE(GetAlignedSize(5, 4) == 8);
        REQUIRE(GetAlignedSize(7, 8) == 8);
        REQUIRE(GetAlignedSize(9, 8) == 16);
    }

    SECTION("Zero value") {
        REQUIRE(GetAlignedSize(0, 256) == 0);
    }

    SECTION("Power of 2 alignments") {
        REQUIRE(GetAlignedSize(100, 64) == 128);
        REQUIRE(GetAlignedSize(100, 128) == 128);
        REQUIRE(GetAlignedSize(200, 128) == 256);
    }
}

TEST_CASE("SubresourceRange creation", "[renderer][vk-helpers]") {
    SECTION("Basic subresource range") {
        auto range = SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

        REQUIRE(range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
        REQUIRE(range.baseMipLevel == 0);
        REQUIRE(range.levelCount == VK_REMAINING_MIP_LEVELS);
        REQUIRE(range.baseArrayLayer == 0);
        REQUIRE(range.layerCount == VK_REMAINING_ARRAY_LAYERS);
    }

    SECTION("Depth aspect") {
        auto range = SubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT);

        REQUIRE(range.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    SECTION("Specific mip and layer counts") {
        auto range = SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 2, 3);

        REQUIRE(range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
        REQUIRE(range.levelCount == 2);
        REQUIRE(range.layerCount == 3);
    }

    SECTION("Full subresource range with base levels") {
        auto range = SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 1, 3, 2, 4);

        REQUIRE(range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
        REQUIRE(range.baseMipLevel == 1);
        REQUIRE(range.levelCount == 3);
        REQUIRE(range.baseArrayLayer == 2);
        REQUIRE(range.layerCount == 4);
    }
}

TEST_CASE("GenerateViewport creates correct viewport", "[renderer][vk-helpers]") {
    SECTION("1920x1080 viewport") {
        auto viewport = GenerateViewport(1920, 1080);

        REQUIRE(viewport.x == 0.0f);
        REQUIRE(viewport.y == 1080.0f); // Inverted: y starts at height
        REQUIRE(viewport.width == 1920.0f);
        REQUIRE(viewport.height == -1080.0f); // Inverted: negative height
        REQUIRE(viewport.minDepth == 0.0f);
        REQUIRE(viewport.maxDepth == 1.0f);
    }

    SECTION("Small viewport") {
        auto viewport = GenerateViewport(256, 256);

        REQUIRE(viewport.width == 256.0f);
        REQUIRE(viewport.y == 256.0f); // Inverted: y starts at height
        REQUIRE(viewport.height == -256.0f); // Inverted: negative height
    }

    SECTION("Asymmetric viewport") {
        auto viewport = GenerateViewport(2560, 1440);

        REQUIRE(viewport.width == 2560.0f);
        REQUIRE(viewport.y == 1440.0f); // Inverted: y starts at height
        REQUIRE(viewport.height == -1440.0f); // Inverted: negative height
    }
}

TEST_CASE("GenerateScissor creates correct scissor", "[renderer][vk-helpers]") {
    SECTION("1920x1080 scissor") {
        auto scissor = GenerateScissor(1920, 1080);

        REQUIRE(scissor.offset.x == 0);
        REQUIRE(scissor.offset.y == 0);
        REQUIRE(scissor.extent.width == 1920);
        REQUIRE(scissor.extent.height == 1080);
    }

    SECTION("Small scissor") {
        auto scissor = GenerateScissor(512, 512);

        REQUIRE(scissor.extent.width == 512);
        REQUIRE(scissor.extent.height == 512);
    }
}

TEST_CASE("ImageMemoryBarrier creation", "[renderer][vk-helpers]") {
    SECTION("Basic transition barrier") {
        VkImage testImage = reinterpret_cast<VkImage>(0x1234);
        auto range = SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

        auto barrier = ImageMemoryBarrier(
            testImage,
            range,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_ACCESS_2_NONE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );

        REQUIRE(barrier.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
        REQUIRE(barrier.image == testImage);
        REQUIRE(barrier.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
        REQUIRE(barrier.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        REQUIRE(barrier.srcStageMask == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
        REQUIRE(barrier.dstStageMask == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
        REQUIRE(barrier.srcAccessMask == VK_ACCESS_2_NONE);
        REQUIRE(barrier.dstAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
    }
}

TEST_CASE("BufferMemoryBarrier creation", "[renderer][vk-helpers]") {
    SECTION("Basic buffer barrier") {
        VkBuffer testBuffer = reinterpret_cast<VkBuffer>(0x5678);

        auto barrier = BufferMemoryBarrier(
            testBuffer,
            0,
            VK_WHOLE_SIZE,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT
        );

        REQUIRE(barrier.sType == VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
        REQUIRE(barrier.buffer == testBuffer);
        REQUIRE(barrier.offset == 0);
        REQUIRE(barrier.size == VK_WHOLE_SIZE);
        REQUIRE(barrier.srcStageMask == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
        REQUIRE(barrier.dstStageMask == VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
        REQUIRE(barrier.srcAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
        REQUIRE(barrier.dstAccessMask == VK_ACCESS_2_SHADER_READ_BIT);
    }

    SECTION("Partial buffer barrier") {
        VkBuffer testBuffer = reinterpret_cast<VkBuffer>(0x5678);

        auto barrier = BufferMemoryBarrier(
            testBuffer,
            256,
            1024,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT
        );

        REQUIRE(barrier.offset == 256);
        REQUIRE(barrier.size == 1024);
    }
}

TEST_CASE("DependencyInfo creation", "[renderer][vk-helpers]") {
    SECTION("Dependency info with image barrier") {
        VkImage testImage = reinterpret_cast<VkImage>(0x1234);
        auto range = SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
        auto barrier = ImageMemoryBarrier(
            testImage, range,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );

        auto depInfo = DependencyInfo(&barrier);

        REQUIRE(depInfo.sType == VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        REQUIRE(depInfo.imageMemoryBarrierCount == 1);
        REQUIRE(depInfo.pImageMemoryBarriers == &barrier);
    }
}

TEST_CASE("CommandPoolCreateInfo", "[renderer][vk-helpers]") {
    SECTION("Command pool for graphics queue") {
        auto info = CommandPoolCreateInfo(0);

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        REQUIRE(info.queueFamilyIndex == 0);
    }

    SECTION("Command pool for compute queue") {
        auto info = CommandPoolCreateInfo(1);

        REQUIRE(info.queueFamilyIndex == 1);
    }
}

TEST_CASE("CommandBufferAllocateInfo", "[renderer][vk-helpers]") {
    SECTION("Allocate single command buffer") {
        auto info = CommandBufferAllocateInfo(1);

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        REQUIRE(info.commandBufferCount == 1);
        REQUIRE(info.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    }

    SECTION("Allocate multiple command buffers") {
        auto info = CommandBufferAllocateInfo(4);

        REQUIRE(info.commandBufferCount == 4);
    }

    SECTION("With command pool") {
        VkCommandPool pool = reinterpret_cast<VkCommandPool>(0xABCD);
        auto info = CommandBufferAllocateInfo(2, pool);

        REQUIRE(info.commandPool == pool);
        REQUIRE(info.commandBufferCount == 2);
    }
}

TEST_CASE("FenceCreateInfo", "[renderer][vk-helpers]") {
    SECTION("Basic fence") {
        auto info = FenceCreateInfo();

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    }
}

TEST_CASE("SemaphoreCreateInfo", "[renderer][vk-helpers]") {
    SECTION("Basic semaphore") {
        auto info = SemaphoreCreateInfo();

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    }
}

TEST_CASE("CommandBufferBeginInfo", "[renderer][vk-helpers]") {
    SECTION("Basic begin info") {
        auto info = CommandBufferBeginInfo();

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    }
}

TEST_CASE("CommandBufferSubmitInfo", "[renderer][vk-helpers]") {
    SECTION("Submit info for command buffer") {
        VkCommandBuffer cmd = reinterpret_cast<VkCommandBuffer>(0xDEAD);
        auto info = CommandBufferSubmitInfo(cmd);

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
        REQUIRE(info.commandBuffer == cmd);
    }
}

TEST_CASE("SemaphoreSubmitInfo", "[renderer][vk-helpers]") {
    SECTION("Wait semaphore info") {
        VkSemaphore sem = reinterpret_cast<VkSemaphore>(0xBEEF);
        auto info = SemaphoreSubmitInfo(sem, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        REQUIRE(info.semaphore == sem);
        REQUIRE(info.stageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    }
}

TEST_CASE("ImageCreateInfo", "[renderer][vk-helpers]") {
    SECTION("Basic 2D image") {
        VkExtent3D extent{1920, 1080, 1};
        auto info = ImageCreateInfo(
            VK_FORMAT_R8G8B8A8_UNORM,
            extent,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        );

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        REQUIRE(info.format == VK_FORMAT_R8G8B8A8_UNORM);
        REQUIRE(info.extent.width == 1920);
        REQUIRE(info.extent.height == 1080);
        REQUIRE(info.extent.depth == 1);
        REQUIRE((info.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0);
        REQUIRE((info.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
    }
}

TEST_CASE("ImageViewCreateInfo", "[renderer][vk-helpers]") {
    SECTION("Basic image view") {
        VkImage img = reinterpret_cast<VkImage>(0xCAFE);
        auto info = ImageViewCreateInfo(
            img,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        REQUIRE(info.image == img);
        REQUIRE(info.format == VK_FORMAT_R8G8B8A8_UNORM);
        REQUIRE(info.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

TEST_CASE("PipelineShaderStageCreateInfo", "[renderer][vk-helpers]") {
    SECTION("Vertex shader stage") {
        VkShaderModule shader = reinterpret_cast<VkShaderModule>(0x9999);
        auto info = PipelineShaderStageCreateInfo(shader, VK_SHADER_STAGE_VERTEX_BIT);

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        REQUIRE(info.module == shader);
        REQUIRE(info.stage == VK_SHADER_STAGE_VERTEX_BIT);
    }

    SECTION("Fragment shader stage") {
        VkShaderModule shader = reinterpret_cast<VkShaderModule>(0x8888);
        auto info = PipelineShaderStageCreateInfo(shader, VK_SHADER_STAGE_FRAGMENT_BIT);

        REQUIRE(info.stage == VK_SHADER_STAGE_FRAGMENT_BIT);
    }
}

TEST_CASE("RenderingAttachmentInfo", "[renderer][vk-helpers]") {
    SECTION("Color attachment with clear") {
        VkImageView view = reinterpret_cast<VkImageView>(0x7777);
        VkClearValue clear{};
        clear.color = {0.0f, 0.0f, 0.0f, 1.0f};

        auto info = RenderingAttachmentInfo(view, &clear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        REQUIRE(info.imageView == view);
        REQUIRE(info.imageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        REQUIRE(info.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
    }

    SECTION("Depth attachment without clear") {
        VkImageView view = reinterpret_cast<VkImageView>(0x6666);

        auto info = RenderingAttachmentInfo(view, nullptr, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        REQUIRE(info.imageView == view);
        REQUIRE(info.imageLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        REQUIRE(info.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
    }
}

TEST_CASE("RenderingInfo", "[renderer][vk-helpers]") {
    SECTION("Rendering info with color and depth") {
        VkExtent2D extent{1920, 1080};
        VkImageView colorView = reinterpret_cast<VkImageView>(0x5555);
        VkImageView depthView = reinterpret_cast<VkImageView>(0x4444);

        VkRenderingAttachmentInfo colorAttachment = RenderingAttachmentInfo(colorView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo depthAttachment = RenderingAttachmentInfo(depthView, nullptr, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        auto info = RenderingInfo(extent, &colorAttachment, &depthAttachment);

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_RENDERING_INFO);
        REQUIRE(info.renderArea.extent.width == 1920);
        REQUIRE(info.renderArea.extent.height == 1080);
        REQUIRE(info.colorAttachmentCount == 1);
        REQUIRE(info.pColorAttachments == &colorAttachment);
        REQUIRE(info.pDepthAttachment == &depthAttachment);
    }
}

TEST_CASE("PresentInfo", "[renderer][vk-helpers]") {
    SECTION("Present info") {
        VkSwapchainKHR swapchain = reinterpret_cast<VkSwapchainKHR>(0x3333);
        VkSemaphore semaphore = reinterpret_cast<VkSemaphore>(0x2222);
        uint32_t imageIndex = 1;

        auto info = PresentInfo(&swapchain, &semaphore, &imageIndex);

        REQUIRE(info.sType == VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        REQUIRE(info.swapchainCount == 1);
        REQUIRE(info.pSwapchains == &swapchain);
        REQUIRE(info.waitSemaphoreCount == 1);
        REQUIRE(info.pWaitSemaphores == &semaphore);
        REQUIRE(info.pImageIndices == &imageIndex);
    }
}

TEST_CASE("Barrier conversion functions", "[renderer][vk-helpers]") {
    SECTION("ToVkBarrier and FromVkBarrier for BufferAcquireOperation") {
        Core::BufferAcquireOperation op{
            .buffer = 0x12345678,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .offset = 256,
            .size = 1024,
            .srcQueueFamilyIndex = 0,
            .dstQueueFamilyIndex = 0
        };

        auto vkBarrier = ToVkBarrier(op);
        auto convertedBack = FromVkBarrier(vkBarrier);

        REQUIRE(convertedBack.buffer == op.buffer);
        REQUIRE(convertedBack.srcStageMask == op.srcStageMask);
        REQUIRE(convertedBack.srcAccessMask == op.srcAccessMask);
        REQUIRE(convertedBack.dstStageMask == op.dstStageMask);
        REQUIRE(convertedBack.dstAccessMask == op.dstAccessMask);
        REQUIRE(convertedBack.offset == op.offset);
        REQUIRE(convertedBack.size == op.size);
        REQUIRE(convertedBack.srcQueueFamilyIndex == op.srcQueueFamilyIndex);
        REQUIRE(convertedBack.dstQueueFamilyIndex == op.dstQueueFamilyIndex);
    }

    SECTION("ToVkBarrier and FromVkBarrier for ImageAcquireOperation") {
        Core::ImageAcquireOperation op{
            .image = 0x87654321,
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
            .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .oldLayout = static_cast<uint32_t>(VK_IMAGE_LAYOUT_UNDEFINED),
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .newLayout = static_cast<uint32_t>(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
            .srcQueueFamilyIndex = 0,
            .dstQueueFamilyIndex = 0
        };

        auto vkBarrier = ToVkBarrier(op);
        auto convertedBack = FromVkBarrier(vkBarrier);

        REQUIRE(convertedBack.image == op.image);
        REQUIRE(convertedBack.aspectMask == op.aspectMask);
        REQUIRE(convertedBack.baseMipLevel == op.baseMipLevel);
        REQUIRE(convertedBack.levelCount == op.levelCount);
        REQUIRE(convertedBack.baseArrayLayer == op.baseArrayLayer);
        REQUIRE(convertedBack.layerCount == op.layerCount);
        REQUIRE(convertedBack.srcStageMask == op.srcStageMask);
        REQUIRE(convertedBack.srcAccessMask == op.srcAccessMask);
        REQUIRE(convertedBack.oldLayout == op.oldLayout);
        REQUIRE(convertedBack.dstStageMask == op.dstStageMask);
        REQUIRE(convertedBack.dstAccessMask == op.dstAccessMask);
        REQUIRE(convertedBack.newLayout == op.newLayout);
        REQUIRE(convertedBack.srcQueueFamilyIndex == op.srcQueueFamilyIndex);
        REQUIRE(convertedBack.dstQueueFamilyIndex == op.dstQueueFamilyIndex);
    }
}
