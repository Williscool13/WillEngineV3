//
// Created by William on 2026-09-04.
//

#include "string_id_table.h"

#include <cassert>
#include <cstring>

namespace Core
{
const char* FindStringLiteral(const uint64_t hash)
{
    size_t lo = 0;
    size_t hi = gSTRING_LITERAL_COUNT;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const uint64_t key = gSTRING_LITERALS[mid].id.id;
        if (key < hash) {
            lo = mid + 1;
        }
        else if (key > hash) {
            hi = mid;
        }
        else {
            return gSTRING_LITERALS[mid].str;
        }
    }
    return nullptr;
}

void VerifyStringLiteralTable()
{
    for (size_t i = 1; i < gSTRING_LITERAL_COUNT; ++i) {
        const uint64_t prev = gSTRING_LITERALS[i - 1].id.id;
        const uint64_t cur = gSTRING_LITERALS[i].id.id;
        assert(prev <= cur && "string literal table not sorted by hash, scripts/xxh3.py has drifted from the engine's XXH3");
        if (prev == cur) {
            assert(strcmp(gSTRING_LITERALS[i - 1].str, gSTRING_LITERALS[i].str) == 0 && "StringID hash collision between two distinct literals");
        }
    }
}
}
