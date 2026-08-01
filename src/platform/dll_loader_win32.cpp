//
// Created by William on 2025-12-14.
//

#include <Windows.h>

#include <cstring>

#include "dll_loader.h"
#include "file_utils.h"
#include "core/containers/inline_string.h"
#include "spdlog/spdlog.h"

namespace Platform
{
bool DllLoader::Load(const char* dllPath, const char* tempCopyName)
{
    originalPath = Core::Path(dllPath);

    lastWriteTime = GetFileWriteTime(dllPath);
    if (lastWriteTime == 0) {
        SPDLOG_WARN("Failed to get DLL timestamp: {}", dllPath);
    }

    if (tempCopyName && tempCopyName[0] != '\0') {
        Core::Path tempDir = originalPath.Parent() / "gamedlls";
        CreateDirectories(tempDir.c_str());

        // A second instance must not contend for a temp file the first has locked
        const Core::InlineString<128> instanceName = Core::InlineString<128>::Format("%lu_%s", static_cast<unsigned long>(GetCurrentProcessId()), tempCopyName);
        Core::Path dstPath = tempDir / instanceName.c_str();

        // Copies left by dead instances
        Core::Path stale[16];
        const uint32_t staleCount = FindFilesByExtension(tempDir, ".dll", stale, 16);
        for (uint32_t i = 0; i < staleCount; i++) {
            if (strcmp(stale[i].c_str(), dstPath.c_str()) != 0) {
                DeleteSingleFile(stale[i]);
            }
        }

        if (!FileCopy(dllPath, dstPath.c_str())) {
            SPDLOG_ERROR("Failed to copy DLL: {} -> {}", dllPath, dstPath.c_str());
            return false;
        }

        loadedPath = dstPath;
    }
    else {
        loadedPath = Core::Path(dllPath);
    }

    handle = LoadLibraryA(loadedPath.c_str());
    if (!handle) {
        SPDLOG_ERROR("Failed to load DLL: {}", loadedPath.c_str());
        return false;
    }

    SPDLOG_DEBUG("Loaded DLL: {}", loadedPath.c_str());
    return true;
}

void DllLoader::Unload()
{
    if (handle) {
        FreeLibrary(handle);
        handle = nullptr;
        SPDLOG_DEBUG("Unloaded DLL: {}", loadedPath.c_str());
    }
}

DllLoadResponse DllLoader::Reload()
{
    uint64_t currentWriteTime = GetFileWriteTime(originalPath.c_str());

    if (currentWriteTime == 0) {
        SPDLOG_ERROR("Failed to check DLL timestamp: {}", originalPath.c_str());
        return DllLoadResponse::FailedToLoad;
    }

    if (currentWriteTime == lastWriteTime) {
        SPDLOG_DEBUG("DLL unchanged, skipping reload");
        return DllLoadResponse::NoChanges;
    }

    Unload();
    bool res = Load(originalPath.c_str(), "game_temp.dll");
    return res ? DllLoadResponse::Loaded : DllLoadResponse::FailedToLoad;
}
}
