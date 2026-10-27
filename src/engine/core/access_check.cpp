//
// Created by William on 2026-10-27.
//

#include "access_check.h"

#if WILL_ACCESS_CHECK

#include <cassert>
#include <mutex>

#include "core/containers/inline_set.h"
#include "core/containers/inline_string.h"
#include "engine/logging/engine_log.h"
#include "engine/systems/system_graph.h"

namespace Engine
{
struct RunningSystem
{
    const char* name{nullptr};
    const AccessSet* access{nullptr};
};

static thread_local RunningSystem gRunningSystem{};
static std::mutex gReportMutex;
static Core::InlineSet<uint64_t, 1024> gReported;

static bool IsChecked()
{
    return gRunningSystem.access != nullptr && !gRunningSystem.access->bExclusive;
}

static bool Declares(const Core::InlineVector<StringID, MAX_ACCESS_ENTRIES>& ids, StringID id)
{
    for (const StringID declared : ids) {
        if (declared == id) { return true; }
    }
    return false;
}

static void Report(const char* what, std::string_view subject)
{
    const uint64_t key = Hash(gRunningSystem.name, StringIdLength(gRunningSystem.name)) ^ (Hash(subject.data(), subject.size()) * 31) ^ Hash(what, StringIdLength(what));
    {
        std::lock_guard lock(gReportMutex);
        if (!gReported.Insert(key)) { return; }
    }
    LOG_ERROR(Engine, "[AccessCheck] {} {} {}", gRunningSystem.name, what, subject);
    assert(false && "SystemGraph access violation, see the log");
}

void SetRunningSystem(const char* name, const AccessSet* access)
{
    gRunningSystem = {name, access};
}

void CheckReadAccess(StringID id)
{
    if (!IsChecked()) { return; }
    if (Declares(gRunningSystem.access->reads, id) || Declares(gRunningSystem.access->writes, id)) { return; }
    Report("reads undeclared", id.ToString());
}

void CheckWriteAccess(StringID id)
{
    if (!IsChecked()) { return; }
    if (Declares(gRunningSystem.access->writes, id)) { return; }
    Report(Declares(gRunningSystem.access->reads, id) ? "writes read-only" : "writes undeclared", id.ToString());
}

void CheckUnnamedAccess(std::string_view typeName)
{
    if (!IsChecked()) { return; }
    Report("touches a type without COMPONENT_NAME:", typeName);
}

void CheckStructuralAccess(const char* operation)
{
    if (!IsChecked()) { return; }
    Report("is non-exclusive but calls", operation);
}
} // Engine

#endif
