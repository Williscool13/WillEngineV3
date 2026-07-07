//
// Created by William on 2026-04-06.
//

#include "engine/resources/model/model_types.h"

#include "render/resource_manager.h"

namespace Engine
{
void StaticModelData::Reset(Render::ResourceManager* resourceManager)
{
    for (auto& mesh : meshes) {
        for (auto& prim : mesh.primitiveProperties) {
            if (prim.blasHandle != 0) {
                vkDestroyAccelerationStructureKHR(resourceManager->context->device, reinterpret_cast<VkAccelerationStructureKHR>(prim.blasHandle), nullptr);
                prim.blasHandle = 0;
            }
            if (prim.blasAllocation.offset != OffsetAllocator::Allocation::NO_SPACE) {
                std::lock_guard lock(resourceManager->blasBufferAllocatorMutex);
                resourceManager->blasBufferAllocator.free(prim.blasAllocation);
            }
            prim.blasAllocation = {};
        }
    }

    meshes.Reset();
    nodes.Reset();
    materials.Reset();
    emissiveTriangles.Reset();

    {
        std::lock_guard lock(resourceManager->vertexBufferAllocatorMutex);
        resourceManager->vertexBufferAllocator.free(vertexPositionAllocation);
    }
    {
        std::lock_guard lock(resourceManager->indexBufferAllocatorMutex);
        resourceManager->indexBufferAllocator.free(indexAllocation);
    }
    {
        std::lock_guard lock(resourceManager->meshletVertexBufferAllocatorMutex);
        resourceManager->meshletVertexBufferAllocator.free(meshletVertexAllocation);
    }
    {
        std::lock_guard lock(resourceManager->meshletTriangleBufferAllocatorMutex);
        resourceManager->meshletTriangleBufferAllocator.free(meshletTriangleAllocation);
    }
    {
        std::lock_guard lock(resourceManager->meshletBufferAllocatorMutex);
        resourceManager->meshletBufferAllocator.free(meshletAllocation);
    }
    {
        std::lock_guard lock(resourceManager->primitiveBufferAllocatorMutex);
        resourceManager->primitiveBufferAllocator.free(primitiveAllocation);
    }
    vertexPositionAllocation = {};
    indexAllocation = {};
    meshletVertexAllocation = {};
    meshletTriangleAllocation = {};
    meshletAllocation = {};
    primitiveAllocation = {};
}
}
