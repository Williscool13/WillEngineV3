//
// Created by William on 2026-10-27.
//

#ifndef WILL_ENGINE_DEFERRED_COMMANDS_H
#define WILL_ENGINE_DEFERRED_COMMANDS_H

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

#include <entt/entt.hpp>

#include "core/containers/vector.h"

namespace Core
{
class TlsfAllocator;
}

namespace Engine
{
class Registry;

/** Structural registry calls recorded by a non-exclusive system; the graph applies them in record order after the system's wave. */
class DeferredCommands
{
public:
    using ApplyFn = void(*)(Registry& registry, entt::entity entity, void* payload);

    DeferredCommands() = default;
    DeferredCommands(const DeferredCommands&) = delete;
    DeferredCommands& operator=(const DeferredCommands&) = delete;
    ~DeferredCommands();

    void Init(Core::TlsfAllocator* allocator_);

    template<typename T, typename... Args>
    void Set(entt::entity entity, Args&&... args)
    {
        if constexpr (std::is_empty_v<T>) {
            Push(&ApplySet<T>, entity, 0);
        }
        else {
            void* payload = Push(&ApplySet<T>, entity, sizeof(T));
            if constexpr (std::is_aggregate_v<T>) { new(payload) T{std::forward<Args>(args)...}; }
            else { new(payload) T(std::forward<Args>(args)...); }
        }
    }

    template<typename... T>
    void Remove(entt::entity entity) { Push(&ApplyRemove<T...>, entity, 0); }

    void Destroy(entt::entity entity) { Push(nullptr, entity, 0); }

    /** Records a call to fn at apply time; fn owns destroying the payload. */
    template<typename T>
    void Record(ApplyFn fn, entt::entity entity, T&& payload)
    {
        using Payload = std::remove_cvref_t<T>;
        new(Push(fn, entity, sizeof(Payload))) Payload(std::forward<T>(payload));
    }

    void ApplyChanges(Registry& registry);

    /** Empties the buffer. */
    void ApplyDestroys(Registry& registry);

    [[nodiscard]] bool IsEmpty() const { return chunks.IsEmpty() || (current == 0 && chunks[0].used == 0); }

private:
    static constexpr size_t RECORD_ALIGN = 16;
    static constexpr uint32_t CHUNK_BYTES = 16 * 1024;

    struct alignas(RECORD_ALIGN) RecordHeader
    {
        ApplyFn apply; // null for a destroy

        entt::entity entity;
        uint32_t stride;
    };

    struct Chunk
    {
        std::byte* data{nullptr};
        uint32_t used{0};
        uint32_t capacity{0};
    };

    void* Push(ApplyFn fn, entt::entity entity, size_t payloadSize);

    template<typename T>
    static void ApplySet(Registry& registry, entt::entity entity, void* payload);

    template<typename... T>
    static void ApplyRemove(Registry& registry, entt::entity entity, void* payload);

    Core::TlsfAllocator* allocator{nullptr};
    Core::Vector<Chunk> chunks;
    uint32_t current{0};
};

/** Every buffer's changes in order, then every buffer's destroys. Records for entities destroyed since recording are skipped. */
void ApplyDeferredCommands(Registry& registry, DeferredCommands* const* buffers, uint32_t count);
} // Engine

#endif //WILL_ENGINE_DEFERRED_COMMANDS_H
