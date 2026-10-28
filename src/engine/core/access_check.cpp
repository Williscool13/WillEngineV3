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

static bool FirstReport(uint64_t key)
{
    std::lock_guard lock(gReportMutex);
    return gReported.Insert(key);
}

static void Report(const char* system, const char* what, std::string_view subject)
{
    if (!FirstReport(Hash(system, StringIdLength(system)) ^ (Hash(subject.data(), subject.size()) * 31) ^ Hash(what, StringIdLength(what)))) { return; }
    LOG_ERROR(Engine, "[AccessCheck] {} {} {}", system, what, subject);
    assert(false && "SystemGraph access violation, see the log");
}

void CheckReadAccess(const char* system, const AccessSet& access, StringID id)
{
    if (Declares(access.reads, id) || Declares(access.writes, id) || Declares(access.deferredWrites, id)) { return; }
    Report(system, "reads undeclared", id.ToString());
}

void CheckWriteAccess(const char* system, const AccessSet& access, StringID id)
{
    if (Declares(access.writes, id)) { return; }
    const char* what = Declares(access.deferredWrites, id) ? "mutates in place, but declares only DeferredWrites for" : Declares(access.reads, id) ? "writes read-only" : "writes undeclared";
    Report(system, what, id.ToString());
}

void CheckDeferredWriteAccess(const char* system, const AccessSet& access, StringID id)
{
    if (Declares(access.writes, id) || Declares(access.deferredWrites, id)) { return; }
    Report(system, Declares(access.reads, id) ? "writes read-only" : "writes undeclared", id.ToString());
}

void ReportDeferredWriteCollision(const char* first, const char* second, StringID id, uint32_t entity)
{
    if (!FirstReport(Hash(first, StringIdLength(first)) ^ (Hash(second, StringIdLength(second)) * 31) ^ (id.id * 17))) { return; }
    LOG_WARN(Engine, "[AccessCheck] {} and {} both set {} on entity {} in one wave; {} wins. Add After<> if {} needs {}'s result", first, second, id.ToString(), entity, second, second, first);
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
