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

    if (!tempCopyName.empty()) {
        std::filesystem::path srcPath(dllPath);
        std::filesystem::path dstPath = srcPath.parent_path() / tempCopyName;

        std::error_code ec;
        std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing, ec);

        if (ec) {
            SPDLOG_ERROR("Failed to copy DLL: {}", ec.message());
            return false;
        }

        loadedPath = dstPath.string();

        std::filesystem::path pdbSrc = srcPath;
        pdbSrc.replace_extension(".pdb");
        std::filesystem::path pdbDst = dstPath;
        pdbDst.replace_extension(".pdb");

        if (std::filesystem::exists(pdbSrc)) {
            std::filesystem::copy_file(pdbSrc, pdbDst, std::filesystem::copy_options::overwrite_existing, ec);
        }
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

bool DllLoader::Reload()
{
    Unload();
    return Load(originalPath, loadedPath != originalPath ? std::filesystem::path(loadedPath).filename().string() : "");
}
}
