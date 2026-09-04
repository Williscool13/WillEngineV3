//
// Created by William on 2026-02-22.
//

#include "string_id.h"

#include <atomic>
#include <cassert>
#include <cstring>

#include "string_id_table.h"

#ifdef WDEBUG

void (*gInternStringFn)(uint64_t, const char*) = nullptr;
const char* (*gResolveStringIdFn)(uint64_t) = nullptr;

static constexpr size_t INTERN_BUFFER_SIZE = 1024 * 1024;
static constexpr size_t INTERN_TABLE_CAP = 65536;
static char gInternBuffer[INTERN_BUFFER_SIZE];
static std::atomic<size_t> gInternHead{0};
static std::atomic<uint64_t> gInternKeys[INTERN_TABLE_CAP];
// offsets store offset+1. 0 = slot claimed but string not yet published
static std::atomic<uint32_t> gInternOffsets[INTERN_TABLE_CAP];

void DBG_InternString(uint64_t hash, const char* str)
{
    if (hash == 0) { return; }
    if (Core::FindStringLiteral(hash)) { return; }
    if (gInternStringFn) {
        gInternStringFn(hash, str);
        return;
    }

    size_t i = hash & (INTERN_TABLE_CAP - 1);
    for (size_t probes = 0; probes < INTERN_TABLE_CAP; ++probes) {
        const uint64_t key = gInternKeys[i].load(std::memory_order_acquire);
        if (key == hash) {
            const uint32_t offset = gInternOffsets[i].load(std::memory_order_acquire);
            assert((offset == 0 || strcmp(gInternBuffer + (offset - 1), str) == 0) && "StringID hash collision between two distinct runtime strings");
            return;
        }
        if (key == 0) {
            uint64_t expected = 0;
            if (gInternKeys[i].compare_exchange_strong(expected, hash, std::memory_order_acq_rel)) {
                const size_t len = strlen(str) + 1;
                const size_t offset = gInternHead.fetch_add(len, std::memory_order_relaxed);
                if (offset + len > INTERN_BUFFER_SIZE) {
                    assert(false && "runtime StringID intern buffer exhausted, raise INTERN_BUFFER_SIZE");
                    return;
                }

                memcpy(gInternBuffer + offset, str, len);
                gInternOffsets[i].store(static_cast<uint32_t>(offset) + 1, std::memory_order_release);
                return;
            }
            if (expected == hash) { return; }
        }
        i = (i + 1) & (INTERN_TABLE_CAP - 1);
    }

    assert(false && "runtime StringID intern table full, raise INTERN_TABLE_CAP");
}

const char* DBG_ResolveStringId(uint64_t hash)
{
    size_t i = hash & (INTERN_TABLE_CAP - 1);
    for (size_t probes = 0; probes < INTERN_TABLE_CAP; ++probes) {
        const uint64_t key = gInternKeys[i].load(std::memory_order_acquire);
        if (key == hash) {
            const uint32_t offset = gInternOffsets[i].load(std::memory_order_acquire);
            return offset != 0 ? gInternBuffer + (offset - 1) : "unknown";
        }
        if (key == 0) { return "unknown"; }
        i = (i + 1) & (INTERN_TABLE_CAP - 1);
    }
    return "unknown";
}

StringID::StringID(const char* str, size_t len)
    : id(Hash(str, len))
{
    DBG_InternString(id, str);
}

const char* StringID::ToString() const
{
    if (const char* literal = Core::FindStringLiteral(id)) { return literal; }
    if (gResolveStringIdFn) { return gResolveStringIdFn(id); }
    return DBG_ResolveStringId(id);
}

#else // NDEBUG

const char* StringID::ToString() const
{
    return "<release>";
}

#endif // NDEBUG
