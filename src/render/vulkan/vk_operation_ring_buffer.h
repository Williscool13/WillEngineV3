//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_VK_OPERATION_RING_BUFFER_H
#define WILL_ENGINE_VK_OPERATION_RING_BUFFER_H
#include "core/include/render_interface.h"
#include "render/types/render_types.h"

namespace Render
{
class ModelMatrixOperationRingBuffer
{
public:
    void Initialize(size_t capacity_)
    {
        assert((capacity_ & (capacity_ - 1)) == 0 && "Capacity must be a power of 2");
        buffer.resize(capacity_);
        head = 0;
        tail = 0;
        capacity = capacity_;
        mask = capacity_ - 1;
    }

    void Enqueue(const std::vector<Core::ModelMatrixOperation>& operations);

    void ProcessOperations(char* pMappedData, uint32_t discardCount);

    size_t GetSize() const { return tail - head; }
    bool IsFull() const { return (tail - head) >= capacity; }

private:
    std::vector<Core::ModelMatrixOperation> buffer;
    size_t head = 0;
    size_t tail = 0;
    size_t capacity = 0;
    size_t mask = 0;
};

class InstanceOperationRingBuffer
{
public:
    void Initialize(size_t capacity_)
    {
        assert((capacity_ & (capacity_ - 1)) == 0 && "Capacity must be a power of 2");
        buffer.resize(capacity_);
        head = 0;
        tail = 0;
        capacity = capacity_;
        mask = capacity_ - 1;
    }

    void Enqueue(const std::vector<Core::InstanceOperation>& operations);

    void ProcessOperations(char* pMappedData, uint32_t discardCount, int32_t& highestInstanceIndex);

    size_t GetSize() const { return tail - head; }
    bool IsFull() const { return (tail - head) >= capacity; }

private:
    std::vector<Core::InstanceOperation> buffer;
    size_t head = 0;
    size_t tail = 0;
    size_t capacity = 0;
    size_t mask = 0;
};

class JointMatrixOperationRingBuffer
{
public:
    void Initialize(size_t capacity_)
    {
        assert((capacity_ & (capacity_ - 1)) == 0 && "Capacity must be a power of 2");
        buffer.resize(capacity_);
        head = 0;
        tail = 0;
        capacity = capacity_;
        mask = capacity_ - 1;
    }

    void Enqueue(const std::vector<Core::JointMatrixOperation>& operations);

    void ProcessOperations(char* pMappedData, uint32_t discardCount);

    size_t GetSize() const { return tail - head; }
    bool IsFull() const { return (tail - head) >= capacity; }

private:
    std::vector<Core::JointMatrixOperation> buffer;
    size_t head = 0;
    size_t tail = 0;
    size_t capacity = 0;
    size_t mask = 0;
};
} // Render

#endif //WILL_ENGINE_VK_OPERATION_RING_BUFFER_H
