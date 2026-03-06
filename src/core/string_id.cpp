//
// Created by William on 2026-02-22.
//

#include "string_id.h"

#ifndef PACKAGED_BUILD

void (*gInternStringFn)(uint64_t, const char*) = nullptr;
const char* (*gResolveStringIdFn)(uint64_t) = nullptr;

static std::unordered_map<uint64_t, const char*>& GetInternTable()
{
    static std::unordered_map<uint64_t, const char*> table;
    return table;
}

void DBG_InternString(uint64_t hash, const char* str)
{
    auto& table = GetInternTable();
    if (!table.contains(hash)) {
        table[hash] = str;
    }
}

const char* DBG_ResolveStringId(uint64_t hash)
{
    auto& table = GetInternTable();
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

#else // PACKAGED_BUILD

const char* StringID::ToString() const
{
    return "<release>";
}

#endif // PACKAGED_BUILD