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

bool DirectoryWatcher::Start(const char* directory, Callback cb, float debounceSeconds, const char* filterFilename)
{
    callback = std::move(cb);
    debounceTime = debounceSeconds;
    lastTrigger = std::chrono::steady_clock::now();

    filterFilenameW.clear();
    if (filterFilename && filterFilename[0] != '\0') {
        const int len = MultiByteToWideChar(CP_UTF8, 0, filterFilename, -1, nullptr, 0);
        if (len > 0) {
            filterFilenameW.resize(len - 1);
            MultiByteToWideChar(CP_UTF8, 0, filterFilename, -1, filterFilenameW.data(), len);
        }
    }

    handle = CreateFileA(
        directory,
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
        bool matched = filterFilenameW.empty();
        if (!matched) {
            const char* ptr = buffer;
            while (true) {
                const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(ptr);
                const std::wstring_view changedName{info->FileName, info->FileNameLength / sizeof(WCHAR)};
                if (changedName == filterFilenameW) {
                    matched = true;
                    break;
                }
                if (info->NextEntryOffset == 0) { break; }
                ptr += info->NextEntryOffset;
            }
        }

        if (matched) {
            pending = true;
            lastTrigger = std::chrono::steady_clock::now();
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
