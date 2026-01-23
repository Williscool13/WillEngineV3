//
// Created by William on 2025-12-14.
//

#include <filesystem>

#include "dll_loader.h"
#include "spdlog/spdlog.h"

namespace Platform
{
bool DllLoader::Load(const std::string& dllPath, const std::string& tempCopyName)
{
    originalPath = dllPath;

    std::error_code ec;
    lastWriteTime = std::filesystem::last_write_time(dllPath, ec);
    if (ec) {
        SPDLOG_WARN("Failed to get DLL timestamp: {}", ec.message());
    }

    if (!tempCopyName.empty()) {
        std::filesystem::path srcPath(dllPath);

        // Create subdirectory for temp DLLs
        std::filesystem::path tempDir = srcPath.parent_path() / "gamedlls";
        std::filesystem::create_directories(tempDir);

        std::filesystem::path dstPath = tempDir / tempCopyName;

        std::error_code ec;
        std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing, ec);

        if (ec) {
            SPDLOG_ERROR("Failed to copy DLL: {}", ec.message());
            return false;
        }

        loadedPath = dstPath.string();
    }
    else {
        loadedPath = dllPath;
    }

    handle = LoadLibraryA(loadedPath.c_str());
    if (!handle) {
        SPDLOG_ERROR("Failed to load DLL: {}", loadedPath);
        return false;
    }

    SPDLOG_DEBUG("Loaded DLL: {}", loadedPath);
    return true;
}

void DllLoader::Unload()
{
    if (handle) {
        FreeLibrary(handle);
        handle = nullptr;
        SPDLOG_DEBUG("Unloaded DLL: {}", loadedPath);
    }
}

DllLoadResponse DllLoader::Reload()
{
    std::error_code ec;
    auto currentWriteTime = std::filesystem::last_write_time(originalPath, ec);

    if (ec) {
        SPDLOG_ERROR("Failed to check DLL timestamp: {}", ec.message());
        return DllLoadResponse::FailedToLoad;
    }

    if (currentWriteTime == lastWriteTime) {
        SPDLOG_DEBUG("DLL unchanged, skipping reload");
        return DllLoadResponse::NoChanges;
    }

    Unload();
    bool res = Load(originalPath, "game_temp.dll");
    return res ? DllLoadResponse::Loaded : DllLoadResponse::FailedToLoad;
}
}
