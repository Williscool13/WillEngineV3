//
// Created by William on 2025-12-12.
//

#include "vk_operation_ring_buffer.h"

#include "spdlog/spdlog.h"

namespace Render
{
void ModelMatrixOperationRingBuffer::Enqueue(const std::vector<Core::ModelMatrixOperation>& operations)
{
    count += operations.size();
    if (count > capacity) {
        SPDLOG_WARN("ModelMatrix operation buffer has exceeded count limit.");
    }
    for (const Core::ModelMatrixOperation& op : operations) {
        buffer[head] = op;
        head = (head + 1) % capacity;
    }
}

void ModelMatrixOperationRingBuffer::ProcessOperations(char* pMappedData, uint32_t discardCount)
{
    uint32_t newCount = count;
    uint32_t newTail = tail;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t opIndex = (tail + i) % capacity;
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
            newTail = (newTail + 1) % capacity;
            newCount--;
        }
    }
    count = newCount;
    tail = newTail;
}

void InstanceOperationRingBuffer::Enqueue(const std::vector<Core::InstanceOperation>& operations)
{
    count += operations.size();
    if (count > capacity) {
        SPDLOG_WARN("Instance operation buffer has exceeded count limit.");
    }
    for (const Core::InstanceOperation& op : operations) {
        buffer[head] = op;
        head = (head + 1) % capacity;
    }
}

void InstanceOperationRingBuffer::ProcessOperations(char* pMappedData, uint32_t discardCount, uint32_t& highestInstanceIndex)
{
    uint32_t newCount = count;
    uint32_t newTail = tail;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t opIndex = (tail + i) % capacity;
        Core::InstanceOperation& op = buffer[opIndex];

        Instance inst{
            .primitiveIndex = op.primitiveIndex,
            .modelIndex = op.modelIndex,
            .jointMatrixOffset = op.jointMatrixOffset,
            .bIsAllocated = op.bIsAllocated,
        };
        memcpy(pMappedData + sizeof(Instance) * op.index, &inst, sizeof(Instance));
        highestInstanceIndex = glm::max(highestInstanceIndex, op.index + 1);

        op.frames++;
        if (op.frames == discardCount) {
            newTail = (newTail + 1) % capacity;
            newCount--;
        }
    }
    count = newCount;
    tail = newTail;
}

void JointMatrixOperationRingBuffer::Enqueue(const std::vector<Core::JointMatrixOperation>& operations)
{
    count += operations.size();
    if (count > capacity) {
        SPDLOG_WARN("JointMatrix operation buffer has exceeded count limit.");
    }
    for (const Core::JointMatrixOperation& op : operations) {
        buffer[head] = op;
        head = (head + 1) % capacity;
    }
}

void JointMatrixOperationRingBuffer::ProcessOperations(char* pMappedData, uint32_t discardCount)
{
    uint32_t newCount = count;
    uint32_t newTail = tail;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t opIndex = (tail + i) % capacity;
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
            newTail = (newTail + 1) % capacity;
            newCount--;
        }
    }
    count = newCount;
    tail = newTail;
}
} // Render
