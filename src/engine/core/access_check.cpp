//
// Created by William on 2026-10-27.
//

#include "access_check.h"

#if WILL_ACCESS_CHECK

#include <cassert>
#include <mutex>

#include "core/containers/inline_set.h"
#include "engine/core/access_set.h"
#include "engine/logging/engine_log.h"

namespace Engine
{
static std::mutex gReportMutex;
static Core::InlineSet<uint64_t, 1024> gReported;

static bool Declares(const Core::InlineVector<StringID, MAX_ACCESS_ENTRIES>& ids, StringID id)
{
    for (const StringID declared : ids) {
        if (declared == id) { return true; }
    }
    return false;
}

static void Report(const char* system, const char* what, std::string_view subject)
{
    const uint64_t key = Hash(system, StringIdLength(system)) ^ (Hash(subject.data(), subject.size()) * 31) ^ Hash(what, StringIdLength(what));
    {
        std::lock_guard lock(gReportMutex);
        if (!gReported.Insert(key)) { return; }
    }
    LOG_ERROR(Engine, "[AccessCheck] {} {} {}", system, what, subject);
    assert(false && "SystemGraph access violation, see the log");
}

void CheckReadAccess(const char* system, const AccessSet& access, StringID id)
{
    if (Declares(access.reads, id) || Declares(access.writes, id)) { return; }
    Report(system, "reads undeclared", id.ToString());
}

void CheckWriteAccess(const char* system, const AccessSet& access, StringID id)
{
    if (Declares(access.writes, id)) { return; }
    Report(system, Declares(access.reads, id) ? "writes read-only" : "writes undeclared", id.ToString());
}

void CheckUnnamedAccess(const char* system, std::string_view typeName)
{
    Report(system, "touches a type without COMPONENT_NAME:", typeName);
}

void CheckStructuralAccess(const char* system, const char* operation)
{
    Report(system, "is non-exclusive but calls", operation);
}
} // Engine

#endif
