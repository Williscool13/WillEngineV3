//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_DLL_LOADER_H
#define WILL_ENGINE_DLL_LOADER_H

#ifdef _WIN32
#include <windows.h>
using DllHandle = HMODULE;
#else
#include <dlfcn.h>
using DllHandle = void*;
#endif

#include "core/containers/inline_path.h"

namespace Platform
{
enum class DllLoadResponse
{
    Loaded,
    FailedToLoad,
    NoChanges,
};

class DllLoader
{
public:
    DllLoader() = default;

    ~DllLoader() { Unload(); }

    DllLoader(const DllLoader&) = delete;

    DllLoader& operator=(const DllLoader&) = delete;

    bool Load(const char* dllPath, const char* tempCopyName = "");

    void Unload();

    DllLoadResponse Reload();

    template<typename FuncType>
    FuncType GetFunction(const char* functionName)
    {
        if (!handle) return nullptr;
#ifdef _WIN32
        return reinterpret_cast<FuncType>(GetProcAddress(handle, functionName));
#else
        return reinterpret_cast<FuncType>(dlsym(handle, functionName));
#endif
    }

    bool IsLoaded() const { return handle != nullptr; }

private:
    DllHandle handle = nullptr;
    Core::Path originalPath;
    Core::Path loadedPath;
    int32_t reloadCount{0};
    uint64_t lastWriteTime{0};
};
}

#endif // WILL_ENGINE_DLL_LOADER_H
