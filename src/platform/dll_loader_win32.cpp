//
// Created by William on 2025-12-14.
//

#include <Windows.h>

#include "dll_loader.h"
#include "file_utils.h"
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

        Core::Path dstPath = tempDir / tempCopyName;

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
