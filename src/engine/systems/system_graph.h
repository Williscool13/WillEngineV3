//
// Created by William on 2026-09-07.
//

#ifndef WILL_ENGINE_SYSTEM_GRAPH_H
#define WILL_ENGINE_SYSTEM_GRAPH_H

#include <cassert>
#include <cstdint>

#include <enkiTS/src/TaskScheduler.h>
#include <tracy/Tracy.hpp>

#include "core/string_id.h"
#include "core/containers/inline_vector.h"

namespace Core
{
struct FrameBuffer;
}

namespace Engine
{
struct EngineContext;
struct EngineState;

inline constexpr uint32_t MAX_SYSTEM_NODES = 128;
inline constexpr uint32_t MAX_ACCESS_ENTRIES = 16;
inline constexpr uint32_t MAX_NODES_PER_PHASE = 64;
inline constexpr uint32_t MAX_ACCESS_IDS_PER_PHASE = 128;
inline constexpr uint32_t ACCESS_MASK_WORDS = MAX_ACCESS_IDS_PER_PHASE / 64;

enum class SystemPhase : uint8_t
{
    PreUpdate,
    GameUpdate,
    PostUpdate,
    PrepareFrame,
};

using SystemFn = void(*)(EngineContext*, EngineState*);
using SystemFrameFn = void(*)(EngineContext*, EngineState*, Core::FrameBuffer*);

struct AccessSet
{
    bool bExclusive{true};
    Core::InlineVector<StringID, MAX_ACCESS_ENTRIES> reads{};
    Core::InlineVector<StringID, MAX_ACCESS_ENTRIES> writes{};
};

struct SystemNode
{
    const char* name{};
    SystemFn fn{};
    SystemFrameFn frameFn{};
    SystemPhase phase{SystemPhase::PreUpdate};
    AccessSet access{};
};

/**
 * Per-frame system schedule rebuilt every tick.
 */
class SystemGraph
{
public:
    void SetScheduler(enki::TaskScheduler* scheduler_) { scheduler = scheduler_; }

    void BeginFrame()
    {
        nodes.Clear();
        cursor = 0;
    }

    void BeginPhase(SystemPhase phase) { currentPhase = phase; }

    void Add(const char* name, SystemFn fn, const AccessSet& access = {})
    {
        nodes.PushBack({.name = name, .fn = fn, .phase = currentPhase, .access = access});
    }

    void Add(const char* name, SystemFrameFn fn, const AccessSet& access = {})
    {
        nodes.PushBack({.name = name, .frameFn = fn, .phase = currentPhase, .access = access});
    }

    void ExecutePhase(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer)
    {
        const uint32_t begin = static_cast<uint32_t>(cursor);
        const uint32_t count = static_cast<uint32_t>(nodes.Size()) - begin;
        cursor = nodes.Size();
        if (count == 0) { return; }
        assert(count <= MAX_NODES_PER_PHASE && "SystemGraph phase exceeds MAX_NODES_PER_PHASE");

        AccessMask readMasks[MAX_NODES_PER_PHASE];
        AccessMask writeMasks[MAX_NODES_PER_PHASE];
        uint64_t predMasks[MAX_NODES_PER_PHASE];
        StringID ids[MAX_ACCESS_IDS_PER_PHASE];
        uint32_t idCount = 0;

        for (uint32_t i = 0; i < count; ++i) {
            const AccessSet& access = nodes[begin + i].access;
            if (!access.bExclusive) {
                for (const StringID id : access.reads) { readMasks[i].Set(InternAccessBit(ids, idCount, id)); }
                for (const StringID id : access.writes) { writeMasks[i].Set(InternAccessBit(ids, idCount, id)); }
            }
            uint64_t pred = 0;
            for (uint32_t j = 0; j < i; ++j) {
                if (Conflicts(nodes[begin + i].access, nodes[begin + j].access, readMasks[i], writeMasks[i], readMasks[j], writeMasks[j])) {
                    pred |= 1ull << j;
                }
            }
            predMasks[i] = pred;
        }

        const uint64_t allMask = count == 64 ? ~0ull : (1ull << count) - 1;
        uint64_t finished = 0;
        uint32_t wave[MAX_NODES_PER_PHASE];
        while (finished != allMask) {
            uint32_t waveCount = 0;
            for (uint32_t i = 0; i < count; ++i) {
                const uint64_t bit = 1ull << i;
                if ((finished & bit) == 0 && (predMasks[i] & ~finished) == 0) {
                    wave[waveCount++] = begin + i;
                }
            }
            assert(waveCount > 0 && "SystemGraph wave deadlock");

            if (waveCount == 1 || scheduler == nullptr) {
                for (uint32_t t = 0; t < waveCount; ++t) {
                    RunNode(nodes[wave[t]], ctx, state, frameBuffer);
                }
            }
            else {
                for (uint32_t t = 0; t < waveCount; ++t) {
                    assert(!nodes[wave[t]].access.bExclusive && "Exclusive node in a multi-node wave");
                }
                enki::TaskSet waveTask(waveCount, [&](enki::TaskSetPartition range, uint32_t) {
                    for (uint32_t t = range.start; t < range.end; ++t) {
                        RunNode(nodes[wave[t]], ctx, state, frameBuffer);
                    }
                });
                scheduler->AddTaskSetToPipe(&waveTask);
                scheduler->WaitforTask(&waveTask);
            }

            for (uint32_t t = 0; t < waveCount; ++t) { finished |= 1ull << (wave[t] - begin); }
        }
    }

private:
    struct AccessMask
    {
        uint64_t words[ACCESS_MASK_WORDS]{};

        void Set(uint32_t bit) { words[bit / 64] |= 1ull << (bit % 64); }
    };

    static uint32_t InternAccessBit(StringID* ids, uint32_t& idCount, StringID id)
    {
        for (uint32_t b = 0; b < idCount; ++b) {
            if (ids[b] == id) { return b; }
        }
        assert(idCount < MAX_ACCESS_IDS_PER_PHASE && "SystemGraph phase access id overflow");
        ids[idCount] = id;
        return idCount++;
    }

    static bool Conflicts(const AccessSet& a, const AccessSet& b, const AccessMask& aRead, const AccessMask& aWrite, const AccessMask& bRead, const AccessMask& bWrite)
    {
        if (a.bExclusive || b.bExclusive) { return true; }
        for (uint32_t w = 0; w < ACCESS_MASK_WORDS; ++w) {
            if ((aWrite.words[w] & (bWrite.words[w] | bRead.words[w])) != 0 || (aRead.words[w] & bWrite.words[w]) != 0) { return true; }
        }
        return false;
    }

    static void RunNode(const SystemNode& node, EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer)
    {
        ZoneTransientN(zone, node.name, true);
        if (node.frameFn != nullptr) { node.frameFn(ctx, state, frameBuffer); }
        else { node.fn(ctx, state); }
    }

    enki::TaskScheduler* scheduler{};
    Core::InlineVector<SystemNode, MAX_SYSTEM_NODES> nodes{};
    SystemPhase currentPhase{SystemPhase::PreUpdate};
    size_t cursor{};
};
} // Engine

#endif //WILL_ENGINE_SYSTEM_GRAPH_H
