//
// Created by William on 2026-04-06.
//

#include "engine/resources/model/model_types.h"

#include "render/resource_manager.h"

namespace Engine
{
void StaticModelData::Reset(Render::ResourceManager* resourceManager)
{
    meshes.Reset();
    nodes.Reset();
    materials.Reset();

    {
        std::lock_guard lock(resourceManager->vertexBufferAllocatorMutex);
        resourceManager->vertexBufferAllocator.free(vertexAllocation);
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
    vertexAllocation = {};
    meshletVertexAllocation = {};
    meshletTriangleAllocation = {};
    meshletAllocation = {};
    primitiveAllocation = {};
}
}
