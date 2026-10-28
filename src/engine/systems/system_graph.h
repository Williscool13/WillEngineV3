//
// Created by William on 2026-09-07.
//

#ifndef WILL_ENGINE_SYSTEM_GRAPH_H
#define WILL_ENGINE_SYSTEM_GRAPH_H

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <type_traits>

#include <enkiTS/src/TaskScheduler.h>
#include <tracy/Tracy.hpp>

#include "core/string_id.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "core/containers/vector.h"
#include "engine/core/access_set.h"
#include "engine/core/deferred_commands.h"
#include "engine/core/system_registry.h"

namespace Core
{
struct FrameBuffer;
}

namespace Engine
{
struct EngineContext;
struct EngineState;

inline constexpr uint32_t MAX_SYSTEM_NODES = 128;
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

/** When a system runs; the graph drops nodes whose condition does not hold this frame. */
enum class SystemWhen : uint8_t
{
    Always,
    /** Edit mode in the editor: nothing is simulating. */
    Editing,
    /** Play or Simulate, paused or not. */
    Running,
    /** The world steps: Simulate, or Play with input captured. Movers, rotators, spawners. */
    WorldRunning,
    /** Play with input captured: the player and its rules. */
    Gameplay,
};

/** This frame's truth for each SystemWhen, decided once by the engine. */
struct SystemConditions
{
    bool bEditing{true};
    bool bRunning{false};
    bool bWorldRunning{false};
    bool bGameplay{false};
};

using SystemFn = void(*)(EngineContext*, EngineState*, SystemRegistry&);
using SystemFrameFn = void(*)(EngineContext*, EngineState*, SystemRegistry&, Core::FrameBuffer*);

struct SystemNode
{
    const char* name{};
    SystemFn fn{};
    SystemFrameFn frameFn{};
    SystemPhase phase{SystemPhase::PreUpdate};
    AccessSet access{};

    [[nodiscard]] const void* Id() const { return fn != nullptr ? SystemId(fn) : SystemId(frameFn); }
};

/**
 * Per-frame system schedule rebuilt every tick.
 */
class SystemGraph
{
public:
    void SetScheduler(enki::TaskScheduler* scheduler_) { scheduler = scheduler_; }

    void SetRegistry(Registry* storage_) { storage = storage_; }

    void SetAllocator(Core::TlsfAllocator* allocator)
    {
        for (DeferredCommands& commands : commandBuffers) { commands.Init(allocator); }
#if WILL_ACCESS_CHECK
        waveSets = Core::Vector<WaveSet>(allocator, Core::AllocTag::ECS);
#endif
    }

    void SetDumpSink(void (*sink)(EngineState*, const char*)) { dumpSink = sink; }

    /** Receives the dumped frame as Graphviz DOT at the start of the next frame. */
    void SetDotSink(void (*sink)(const char*, size_t)) { dotSink = sink; }

    void RequestDump() { bDumpPending = true; }

    void BeginFrame()
    {
        if (bDumpActive && dotSink != nullptr) {
            Dot().Append("}\n");
            dotSink(Dot().c_str(), Dot().Size());
        }
        nodes.Clear();
        cursor = 0;
        bDumpActive = bDumpPending && dumpSink != nullptr;
        bDumpPending = false;
        if (bDumpActive) {
            Dot().Clear();
            Dot().Append("digraph SystemGraph {\n    rankdir=TB;\n    nodesep=0.4;\n    ranksep=0.5;\n    node [shape=box fontname=\"Consolas\" style=filled];\n");
            Dot().Append("    legend_exclusive [label=\"Exclusive\" fillcolor=\"#eeeeee\"];\n    legend_parallel [label=\"Declared access\" fillcolor=\"#d5e8d4\"];\n");
            Dot().Append("    legend_exclusive -> legend_parallel [style=invis];\n");
            dotPreviousTail = ~0u;
        }
    }

    /** Conditions are re-read per phase, so a run-mode change mid-frame takes effect at the next phase. */
    void BeginPhase(SystemPhase phase, const SystemConditions& conditions_)
    {
        currentPhase = phase;
        conditions = conditions_;
    }

    void Add(const char* name, SystemFn fn, const AccessSet& access = {})
    {
        nodes.PushBack({.name = name, .fn = fn, .phase = currentPhase, .access = access});
    }

    void Add(const char* name, SystemFrameFn fn, const AccessSet& access = {})
    {
        nodes.PushBack({.name = name, .frameFn = fn, .phase = currentPhase, .access = access});
    }

    void Add(const char* name, SystemFn fn, SystemWhen when, const AccessSet& access = {})
    {
        if (Holds(when)) { Add(name, fn, access); }
    }

    void Add(const char* name, SystemFrameFn fn, SystemWhen when, const AccessSet& access = {})
    {
        if (Holds(when)) { Add(name, fn, access); }
    }

    [[nodiscard]] bool Holds(SystemWhen when) const
    {
        switch (when) {
            case SystemWhen::Always: return true;
            case SystemWhen::Editing: return conditions.bEditing;
            case SystemWhen::Running: return conditions.bRunning;
            case SystemWhen::WorldRunning: return conditions.bWorldRunning;
            case SystemWhen::Gameplay: return conditions.bGameplay;
        }
        return false;
    }

    void ExecutePhase(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer)
    {
        const uint32_t begin = static_cast<uint32_t>(cursor);
        const uint32_t count = static_cast<uint32_t>(nodes.Size()) - begin;
        cursor = nodes.Size();
        if (count == 0) { return; }
        assert(count <= MAX_NODES_PER_PHASE && "SystemGraph phase exceeds MAX_NODES_PER_PHASE");

        NodeMasks masks[MAX_NODES_PER_PHASE];
        uint64_t predMasks[MAX_NODES_PER_PHASE];
        uint64_t afterMasks[MAX_NODES_PER_PHASE];
        StringID ids[MAX_ACCESS_IDS_PER_PHASE];
        uint32_t idCount = 0;

        for (uint32_t i = 0; i < count; ++i) {
            const AccessSet& access = nodes[begin + i].access;
            if (!access.bExclusive) {
                for (const StringID id : access.reads) { masks[i].read.Set(InternAccessBit(ids, idCount, id)); }
                for (const StringID id : access.writes) { masks[i].write.Set(InternAccessBit(ids, idCount, id)); }
                for (const StringID id : access.deferredWrites) { masks[i].deferredWrite.Set(InternAccessBit(ids, idCount, id)); }
            }
            uint64_t pred = 0;
            uint64_t after = 0;
            for (uint32_t j = 0; j < i; ++j) {
                if (Conflicts(nodes[begin + i].access, nodes[begin + j].access, masks[i], masks[j])) {
                    pred |= 1ull << j;
                }
                for (const void* target : access.after) {
                    if (target == nodes[begin + j].Id()) { after |= 1ull << j; }
                }
            }
#ifndef NDEBUG
            for (uint32_t j = i + 1; j < count; ++j) {
                for (const void* target : access.after) {
                    assert(target != nodes[begin + j].Id() && "After<> names a system registered later; registration order is the only order");
                }
            }
#endif
            predMasks[i] = pred | after;
            afterMasks[i] = after & ~pred;
        }

        const uint64_t allMask = count == 64 ? ~0ull : (1ull << count) - 1;
        uint64_t finished = 0;
        uint32_t wave[MAX_NODES_PER_PHASE];
        uint32_t waveOfNode[MAX_NODES_PER_PHASE];
        uint32_t waveIndex = 0;
        while (finished != allMask) {
            uint32_t waveCount = 0;
            for (uint32_t i = 0; i < count; ++i) {
                const uint64_t bit = 1ull << i;
                if ((finished & bit) == 0 && (predMasks[i] & ~finished) == 0) {
                    wave[waveCount++] = begin + i;
                }
            }
            assert(waveCount > 0 && "SystemGraph wave deadlock");
            if (bDumpActive) { DumpWave(state, wave, waveCount, waveIndex); }

            ZoneScopedN("Wave");
            ZoneValue(waveIndex);
            ZoneValue(waveCount);
            if (waveCount == 1 || scheduler == nullptr) {
                for (uint32_t t = 0; t < waveCount; ++t) {
                    RunNode(nodes[wave[t]], ctx, state, frameBuffer, CommandsFor(begin, wave[t]));
                }
            }
            else {
                for (uint32_t t = 0; t < waveCount; ++t) {
                    assert(!nodes[wave[t]].access.bExclusive && "Exclusive node in a multi-node wave");
                }
                enki::TaskSet waveTask(waveCount, [&](enki::TaskSetPartition range, uint32_t) {
                    for (uint32_t t = range.start; t < range.end; ++t) {
                        RunNode(nodes[wave[t]], ctx, state, frameBuffer, CommandsFor(begin, wave[t]));
                    }
                });
                scheduler->AddTaskSetToPipe(&waveTask);
                scheduler->WaitforTask(&waveTask);
            }

#if WILL_ACCESS_CHECK
            CheckDeferredWriteCollisions(wave, waveCount, begin);
#endif

            DeferredCommands* waveCommands[MAX_NODES_PER_PHASE];
            uint32_t waveCommandCount = 0;
            for (uint32_t t = 0; t < waveCount; ++t) {
                DeferredCommands* commands = CommandsFor(begin, wave[t]);
                if (commands != nullptr && !commands->IsEmpty()) { waveCommands[waveCommandCount++] = commands; }
            }
            if (waveCommandCount > 0) {
                ZoneScopedN("ApplyDeferredCommands");
                ApplyDeferredCommands(*storage, waveCommands, waveCommandCount);
            }

            for (uint32_t t = 0; t < waveCount; ++t) {
                finished |= 1ull << (wave[t] - begin);
                waveOfNode[wave[t] - begin] = waveIndex;
            }
            ++waveIndex;
        }
        if (bDumpActive && dotSink != nullptr) { DotPhase(begin, count, predMasks, afterMasks, waveOfNode, waveIndex); }
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

    struct NodeMasks
    {
        AccessMask read;
        AccessMask write;
        AccessMask deferredWrite;
    };

    /** DeferredWrites against DeferredWrites is the one write pairing that does not conflict. */
    static bool Conflicts(const AccessSet& a, const AccessSet& b, const NodeMasks& am, const NodeMasks& bm)
    {
        if (a.bExclusive || b.bExclusive) { return true; }
        for (uint32_t w = 0; w < ACCESS_MASK_WORDS; ++w) {
            const uint64_t aAny = am.read.words[w] | am.write.words[w] | am.deferredWrite.words[w];
            const uint64_t bAny = bm.read.words[w] | bm.write.words[w] | bm.deferredWrite.words[w];
            if ((am.write.words[w] & bAny) != 0 || (bm.write.words[w] & aAny) != 0) { return true; }
            if ((am.deferredWrite.words[w] & bm.read.words[w]) != 0 || (bm.deferredWrite.words[w] & am.read.words[w]) != 0) { return true; }
        }
        return false;
    }

#if WILL_ACCESS_CHECK
    struct WaveSet
    {
        StringID type;
        uint32_t entity;
        uint32_t node;
    };

    static bool DeclaresDeferredWrite(const AccessSet& access, StringID id)
    {
        for (const StringID declared : access.deferredWrites) {
            if (declared == id) { return true; }
        }
        return false;
    }

    void CheckDeferredWriteCollisions(const uint32_t* wave, uint32_t waveCount, uint32_t begin)
    {
        uint32_t deferredWriters = 0;
        for (uint32_t t = 0; t < waveCount; ++t) {
            if (!nodes[wave[t]].access.deferredWrites.IsEmpty()) { ++deferredWriters; }
        }
        if (deferredWriters < 2) { return; }

        waveSets.Clear();
        for (uint32_t t = 0; t < waveCount; ++t) {
            const SystemNode& node = nodes[wave[t]];
            if (node.access.deferredWrites.IsEmpty()) { continue; }
            commandBuffers[wave[t] - begin].ForEachSet([&](StringID type, entt::entity entity) {
                if (DeclaresDeferredWrite(node.access, type)) { waveSets.PushBack({type, entt::to_integral(entity), wave[t]}); }
            });
        }
        std::sort(waveSets.begin(), waveSets.end(), [](const WaveSet& a, const WaveSet& b) {
            if (a.type.id != b.type.id) { return a.type.id < b.type.id; }
            if (a.entity != b.entity) { return a.entity < b.entity; }
            return a.node < b.node;
        });
        for (size_t i = 1; i < waveSets.Size(); ++i) {
            const WaveSet& previous = waveSets[i - 1];
            const WaveSet& current = waveSets[i];
            if (previous.type == current.type && previous.entity == current.entity && previous.node != current.node) {
                ReportDeferredWriteCollision(nodes[previous.node].name, nodes[current.node].name, current.type, current.entity);
            }
        }
    }
#endif

    DeferredCommands* CommandsFor(uint32_t begin, uint32_t node)
    {
        return nodes[node].access.bExclusive ? nullptr : &commandBuffers[node - begin];
    }

    void RunNode(const SystemNode& node, EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer, DeferredCommands* commands)
    {
        ZoneTransientN(zone, node.name, true);
        SystemRegistry registry{*storage, node.access, commands, node.name};
        if (node.frameFn != nullptr) { node.frameFn(ctx, state, registry, frameBuffer); }
        else { node.fn(ctx, state, registry); }
    }

    static const char* PhaseName(SystemPhase phase)
    {
        switch (phase) {
            case SystemPhase::PreUpdate: return "PreUpdate";
            case SystemPhase::GameUpdate: return "GameUpdate";
            case SystemPhase::PostUpdate: return "PostUpdate";
            case SystemPhase::PrepareFrame: return "PrepareFrame";
            default: return "?";
        }
    }

    void DumpWave(EngineState* state, const uint32_t* wave, uint32_t waveCount, uint32_t waveIndex)
    {
        for (uint32_t t = 0; t < waveCount; ++t) {
            const SystemNode& node = nodes[wave[t]];
            Core::InlineString<256> line = Core::InlineString<256>::Format("[%s] wave %u: %s", PhaseName(node.phase), waveIndex, node.name);
            if (node.access.bExclusive) {
                line.Append(" (exclusive)");
            }
            else {
                line.Append(" [r:");
                for (const StringID id : node.access.reads) {
                    line.Append(" ");
                    line.Append(id.ToString());
                }
                line.Append(" | w:");
                for (const StringID id : node.access.writes) {
                    line.Append(" ");
                    line.Append(id.ToString());
                }
                if (!node.access.deferredWrites.IsEmpty()) {
                    line.Append(" | dw:");
                    for (const StringID id : node.access.deferredWrites) {
                        line.Append(" ");
                        line.Append(id.ToString());
                    }
                }
                AppendAfterNames(line, " | after:", node.access);
                line.Append("]");
            }
            dumpSink(state, line.c_str());
        }
    }

    static Core::InlineString<65536>& Dot()
    {
        static Core::InlineString<65536> dot;
        return dot;
    }

    template<size_t N>
    void AppendAfterNames(Core::InlineString<N>& line, const char* prefix, const AccessSet& access) const
    {
        if (access.after.IsEmpty()) { return; }
        line.Append(prefix);
        for (const void* target : access.after) {
            for (const SystemNode& other : nodes) {
                if (other.Id() == target) {
                    line.Append(" ");
                    line.Append(other.name);
                    break;
                }
            }
        }
    }

    static void AppendIds(Core::InlineString<1024>& label, const char* prefix, const Core::InlineVector<StringID, MAX_ACCESS_ENTRIES>& ids)
    {
        if (ids.Size() == 0) { return; }
        label.Append(prefix);
        for (const StringID id : ids) {
            label.Append(" ");
            label.Append(id.ToString());
        }
    }

    /** Edges are the transitive reduction of the conflict predecessors. */
    void DotPhase(uint32_t begin, uint32_t count, const uint64_t* predMasks, const uint64_t* afterMasks, const uint32_t* waveOfNode, uint32_t waveCount)
    {
        Core::InlineString<65536>& dot = Dot();
        const uint32_t phase = static_cast<uint32_t>(nodes[begin].phase);
        dot.Append(Core::InlineString<128>::Format("    subgraph cluster_%u {\n        label=\"%s\"; fontname=\"Consolas\";\n", phase, PhaseName(nodes[begin].phase)));
        for (uint32_t i = 0; i < count; ++i) {
            const SystemNode& node = nodes[begin + i];
            Core::InlineString<1024> label = Core::InlineString<1024>::Format("%s\\nwave %u", node.name, waveOfNode[i]);
            if (!node.access.bExclusive) {
                AppendIds(label, "\\nr:", node.access.reads);
                AppendIds(label, "\\nw:", node.access.writes);
                AppendIds(label, "\\ndw:", node.access.deferredWrites);
            }
            dot.Append(Core::InlineString<1200>::Format("        n%u_%u [label=\"%s\" fillcolor=\"%s\"];\n", phase, i, label.c_str(), node.access.bExclusive ? "#eeeeee" : "#d5e8d4"));
        }
        for (uint32_t w = 0; w < waveCount; ++w) {
            dot.Append("        { rank=same;");
            for (uint32_t i = 0; i < count; ++i) {
                if (waveOfNode[i] == w) { dot.Append(Core::InlineString<32>::Format(" n%u_%u;", phase, i)); }
            }
            dot.Append(" }\n");
        }
        dot.Append("    }\n");

        uint64_t ancestors[MAX_NODES_PER_PHASE];
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t all = predMasks[i];
            for (uint32_t j = 0; j < i; ++j) {
                if ((predMasks[i] >> j) & 1ull) { all |= ancestors[j]; }
            }
            ancestors[i] = all;
        }
        uint32_t tail = 0;
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t direct = predMasks[i];
            for (uint32_t k = 0; k < i; ++k) {
                if ((predMasks[i] >> k) & 1ull) { direct &= ~ancestors[k]; }
            }
            for (uint32_t j = 0; j < i; ++j) {
                if ((direct >> j) & 1ull) {
                    const bool bAfter = (afterMasks[i] >> j) & 1ull;
                    dot.Append(Core::InlineString<160>::Format("    n%u_%u -> n%u_%u%s;\n", phase, j, phase, i, bAfter ? " [color=\"#1f6fd1\" penwidth=2 label=\"after\" fontname=\"Consolas\"]" : ""));
                }
            }
            if (waveOfNode[i] + 1 == waveCount) { tail = i; }
        }
        if (dotPreviousTail != ~0u) {
            dot.Append(Core::InlineString<96>::Format("    n%u_%u -> n%u_0 [style=dashed color=\"#999999\"];\n", dotPreviousTail >> 8, dotPreviousTail & 0xFF, phase));
        }
        dotPreviousTail = (phase << 8) | tail;
    }

    enki::TaskScheduler* scheduler{};
    void (*dumpSink)(EngineState*, const char*){};
    void (*dotSink)(const char*, size_t){};
    uint32_t dotPreviousTail{~0u};
    bool bDumpPending{};
    bool bDumpActive{};
    Core::InlineVector<SystemNode, MAX_SYSTEM_NODES> nodes{};
    DeferredCommands commandBuffers[MAX_NODES_PER_PHASE];
    Registry* storage{nullptr};
#if WILL_ACCESS_CHECK
    Core::Vector<WaveSet> waveSets;
#endif
    SystemConditions conditions{};
    SystemPhase currentPhase{SystemPhase::PreUpdate};
    size_t cursor{};
};
} // Engine

#endif //WILL_ENGINE_SYSTEM_GRAPH_H
