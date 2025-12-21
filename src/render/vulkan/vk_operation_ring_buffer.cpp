//
// Created by William on 2025-12-12.
//

#include "vk_operation_ring_buffer.h"

#include "render/shaders/model_interop.h"
#include "spdlog/spdlog.h"

namespace Render
{
void ModelMatrixOperationRingBuffer::Enqueue(const std::vector<Core::ModelMatrixOperation>& operations)
{
    if ((tail - head) + operations.size() > capacity) {
        SPDLOG_WARN("ModelMatrix operation buffer has exceeded capacity limit.");
    }
    for (const Core::ModelMatrixOperation& op : operations) {
        buffer[tail & mask] = op;
        tail++;
    }
}

void ModelMatrixOperationRingBuffer::ProcessOperations(char* pMappedData, uint32_t discardCount)
{
    const size_t count = tail - head;
    size_t processed = 0;

    for (size_t i = 0; i < count; ++i) {
        const size_t opIndex = (head + i) & mask;
        Core::ModelMatrixOperation& op = buffer[opIndex];

        if (op.frames == 0) {
            memcpy(pMappedData + op.index * sizeof(Model) + offsetof(Model, modelMatrix), &op.modelMatrix, sizeof(glm::mat4));
        }
        else {
            memcpy(pMappedData + op.index * sizeof(Model) + offsetof(Model, prevModelMatrix), &op.modelMatrix, sizeof(glm::mat4));
            memcpy(pMappedData + op.index * sizeof(Model) + offsetof(Model, modelMatrix), &op.modelMatrix, sizeof(glm::mat4));
        }

        op.frames++;
        if (op.frames == discardCount) {
            processed++;
        }
    }

    head += processed;
}

void InstanceOperationRingBuffer::Enqueue(const std::vector<Core::InstanceOperation>& operations)
{
    if ((tail - head) + operations.size() > capacity) {
        SPDLOG_WARN("Instance operation buffer has exceeded capacity limit.");
    }
    for (const Core::InstanceOperation& op : operations) {
        buffer[tail & mask] = op;
        tail++;
    }
}

void InstanceOperationRingBuffer::ProcessOperations(char* pMappedData, uint32_t discardCount, int32_t& highestInstanceIndex)
{
    const size_t count = tail - head;
    size_t processed = 0;

    for (size_t i = 0; i < count; ++i) {
        const size_t opIndex = (head + i) & mask;
        Core::InstanceOperation& op = buffer[opIndex];

        Instance inst{
            .primitiveIndex = op.primitiveIndex,
            .modelIndex = op.modelIndex,
            .jointMatrixOffset = op.jointMatrixOffset,
            .bIsAllocated = op.bIsAllocated,
        };
        memcpy(pMappedData + sizeof(Instance) * op.index, &inst, sizeof(Instance));
        highestInstanceIndex = glm::max(highestInstanceIndex, static_cast<int32_t>(op.index));

        op.frames++;
        if (op.frames == discardCount) {
            processed++;
        }
    }

    head += processed;
}

void JointMatrixOperationRingBuffer::Enqueue(const std::vector<Core::JointMatrixOperation>& operations)
{
    if ((tail - head) + operations.size() > capacity) {
        SPDLOG_WARN("JointMatrix operation buffer has exceeded capacity limit.");
    }
    for (const Core::JointMatrixOperation& op : operations) {
        buffer[tail & mask] = op;
        tail++;
    }
}

void JointMatrixOperationRingBuffer::ProcessOperations(char* pMappedData, uint32_t discardCount)
{
    const size_t count = tail - head;
    size_t processed = 0;

    for (size_t i = 0; i < count; ++i) {
        const size_t opIndex = (head + i) & mask;
        Core::JointMatrixOperation& op = buffer[opIndex];

        if (op.frames == 0) {
            memcpy(pMappedData + op.index * sizeof(Model) + offsetof(Model, modelMatrix), &op.jointMatrix, sizeof(glm::mat4));
        }
        else {
            memcpy(pMappedData + op.index * sizeof(Model) + offsetof(Model, prevModelMatrix), &op.jointMatrix, sizeof(glm::mat4));
            memcpy(pMappedData + op.index * sizeof(Model) + offsetof(Model, modelMatrix), &op.jointMatrix, sizeof(glm::mat4));
        }

        op.frames++;
        if (op.frames == discardCount) {
            processed++;
        }
    }

    head += processed;
}
} // Render
