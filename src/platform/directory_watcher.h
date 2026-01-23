//
// Created by William on 2026-01-23.
//

#ifndef WILL_ENGINE_DIRECTORY_WATCHER_H
#define WILL_ENGINE_DIRECTORY_WATCHER_H

#include <chrono>
#include <string>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Platform
{
class DirectoryWatcher
{
public:
    using Callback = std::function<void()>;

    DirectoryWatcher() = default;

    ~DirectoryWatcher();

    DirectoryWatcher(const DirectoryWatcher&) = delete;

    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

    bool Start(const std::string& directory, Callback cb, float debounceSeconds = 0.2f);

    void Stop();

    void Poll();

    float GetTimeSinceLastTrigger() const;

private:
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped{};
    char buffer[4096];
#endif
    Callback callback;
    std::chrono::steady_clock::time_point lastTrigger{};
    float debounceTime = 1.0f;
    bool pending = false;
};
} // Platform

#endif //WILL_ENGINE_DIRECTORY_WATCHER_H
