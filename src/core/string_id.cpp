//
// Created by William on 2026-02-22.
//

#include "string_id.h"

#include <cstring>
#include "memory/linear_allocator.h"

#ifdef WDEBUG

void (*gInternStringFn)(uint64_t, const char*) = nullptr;
const char* (*gResolveStringIdFn)(uint64_t) = nullptr;

static constexpr size_t INTERN_BUFFER_SIZE = 1024 * 1024; // 1MB
static char gInternBuffer[INTERN_BUFFER_SIZE];
static Core::LinearAllocator gInternAllocator{INTERN_BUFFER_SIZE, "StringIdIntern"};

static std::unordered_map<uint64_t, const char*>& GetInternTable()
{
    static std::unordered_map<uint64_t, const char*> table;
    return table;
}

void DBG_InternString(uint64_t hash, const char* str)
{
    auto& table = GetInternTable();
    if (table.contains(hash)) { return; }

    const size_t len = strlen(str) + 1;
    const size_t offset = gInternAllocator.Allocate(len);
    if (offset == SIZE_MAX) { return; }

    memcpy(gInternBuffer + offset, str, len);
    table[hash] = gInternBuffer + offset;
}

const char* DBG_ResolveStringId(uint64_t hash)
{
    const auto& table = GetInternTable();
    auto it = table.find(hash);
    return it != table.end() ? it->second : "unknown";
}

StringID::StringID(const char* str, size_t len)
    : id(fnv1a64(str, len))
{
    if (gInternStringFn) {
        gInternStringFn(id, str);
        return;
    }
    DBG_InternString(id, str);
}

const char* StringID::ToString() const
{
    if (gResolveStringIdFn) {
        return gResolveStringIdFn(id);
    }
    return DBG_ResolveStringId(id);
}

#else // NDEBUG

const char* StringID::ToString() const
{
    return "<release>";
}

#endif // NDEBUG
