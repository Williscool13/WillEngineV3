//
// Created by William on 2026-02-22.
//

#include "string_id.h"

#ifdef DEBUG

void (*gInternStringFn)(uint64_t, const char*) = nullptr;
const char* (*gResolveStringIdFn)(uint64_t) = nullptr;

static std::unordered_map<uint64_t, const char*> gInternTable;

void DBG_InternString(uint64_t hash, const char* str)
{
    if (!gInternTable.contains(hash)) {
        gInternTable[hash] = _strdup(str);
    }
}

const char* DBG_ResolveStringId(uint64_t hash)
{
    const auto it = gInternTable.find(hash);
    return it != gInternTable.end() ? it->second : "<unknown>";
}

StringId::StringId(const char* str, size_t len)
    : id(fnv1a64(str, len))
{
    if (gInternStringFn) {
        gInternStringFn(id, str);
        return;
    }
    DBG_InternString(id, str);
}

const char* StringId::ToString() const
{
    if (gResolveStringIdFn) {
        return gResolveStringIdFn(id);
    }
    return DBG_ResolveStringId(id);
}

#else // !DEBUG

const char* StringId::ToString() const
{
    return "<release>";
}

#endif // DEBUG