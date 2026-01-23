//
// Created by William on 2026-01-23.
//

#include "directory_watcher.h"
#include "spdlog/spdlog.h"

namespace Platform
{
DirectoryWatcher::~DirectoryWatcher()
{
    Stop();
}

bool DirectoryWatcher::Start(const std::string& directory, Callback cb, float debounceSeconds)
{
    callback = std::move(cb);
    debounceTime = debounceSeconds;
    lastTrigger = std::chrono::steady_clock::now();

    handle = CreateFileA(
        directory.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE) {
        SPDLOG_ERROR("Failed to watch directory: {}", directory);
        return false;
    }

    memset(&overlapped, 0, sizeof(overlapped));

    ReadDirectoryChangesW(
        handle,
        buffer,
        sizeof(buffer),
        TRUE,
        FILE_NOTIFY_CHANGE_LAST_WRITE,
        nullptr,
        &overlapped,
        nullptr
    );

    SPDLOG_DEBUG("Watching directory: {}", directory);
    return true;
}

void DirectoryWatcher::Stop()
{
    if (handle != INVALID_HANDLE_VALUE) {
        CancelIo(handle);
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
}

void DirectoryWatcher::Poll()
{
    if (handle == INVALID_HANDLE_VALUE) return;

    DWORD bytes_transferred;
    BOOL result = GetOverlappedResult(handle, &overlapped, &bytes_transferred, FALSE);

    if (result && bytes_transferred > 0) {
        pending = true;
        lastTrigger = std::chrono::steady_clock::now();

        // Restart watch
        memset(&overlapped, 0, sizeof(overlapped));
        ReadDirectoryChangesW(
            handle,
            buffer,
            sizeof(buffer),
            TRUE,
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            nullptr,
            &overlapped,
            nullptr
        );
    }

    // Trigger callback after debounce period
    if (pending) {
        auto elapsed = std::chrono::steady_clock::now() - lastTrigger;
        if (elapsed >= std::chrono::duration<float>(debounceTime)) {
            pending = false;
            callback();
        }
    }
}

float DirectoryWatcher::GetTimeSinceLastTrigger() const
{
    auto elapsed = std::chrono::steady_clock::now() - lastTrigger;
    return std::chrono::duration<float>(elapsed).count();
}
} // Platform
