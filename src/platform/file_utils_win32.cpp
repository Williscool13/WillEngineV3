//
// Created by William on 2026-04-01.
//

#include "file_utils.h"

#include <cstring>
#include <Windows.h>

#include "core/containers/inline_path.h"
#include "core/containers/vector.h"

namespace Platform
{
void CreateDirectories(const char* path)
{
    char buf[1024];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) { len = sizeof(buf) - 1; }
    memcpy(buf, path, len);
    buf[len] = '\0';

    for (char* p = buf + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            CreateDirectoryA(buf, nullptr);
            *p = saved;
        }
    }
    CreateDirectoryA(buf, nullptr);
}

void RemoveDirectories(const char* path)
{
    char buf[1024];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) { len = sizeof(buf) - 1; }
    memcpy(buf, path, len);
    buf[len] = '\0';

    // Ensure path ends without trailing slash for FindFirstFile
    if (len > 0 && (buf[len - 1] == '/' || buf[len - 1] == '\\')) {
        buf[--len] = '\0';
    }

    char search[1024 + 3];
    memcpy(search, buf, len);
    memcpy(search + len, "\\*", 3);

    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search, &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) { continue; }

            char child[1024];
            size_t childLen = len;
            memcpy(child, buf, childLen);
            child[childLen++] = '\\';
            size_t nameLen = strlen(ffd.cFileName);
            memcpy(child + childLen, ffd.cFileName, nameLen + 1);

            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirectories(child);
            } else {
                DeleteFileA(child);
            }
        } while (FindNextFileA(hFind, &ffd));
        FindClose(hFind);
    }

    RemoveDirectoryA(buf);
}

bool CreateEmptyFile(const char* path)
{
    CreateDirectories(path);
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { return false; }
    CloseHandle(h);
    return true;
}

bool DeleteSingleFile(const char* path)
{
    return DeleteFileA(path) != FALSE;
}

bool FileCopy(const char* src, const char* dst)
{
    return CopyFileA(src, dst, FALSE) != FALSE;
}

uint64_t GetFileWriteTime(const char* path)
{
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) { return 0; }
    return (static_cast<uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
}

void RecursiveDirectoryIterator(const Core::Path& dir, Core::Vector<Core::Path>& out)
{
    Core::Path search = dir / "*";
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) { return; }
    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) { continue; }
        Core::Path child = dir / ffd.cFileName;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            RecursiveDirectoryIterator(child, out);
        } else {
            out.PushBack(child);
        }
    } while (FindNextFileA(hFind, &ffd));
    FindClose(hFind);
}

void RecursiveDirectoryIterator(const char* path, Core::Vector<Core::Path>& out)
{
    RecursiveDirectoryIterator(Core::Path(path), out);
}

static uint32_t FindFilesByExtensionImpl(const Core::Path& dir, const char* ext,
    Core::Path* out, uint32_t max, uint32_t count)
{
    Core::Path search = dir / "*";
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) { return count; }
    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) { continue; }
        Core::Path child = dir / ffd.cFileName;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            count = FindFilesByExtensionImpl(child, ext, out, max, count);
        } else if (count < max && child.Extension() == ext) {
            out[count++] = child;
        }
    } while (FindNextFileA(hFind, &ffd) && count < max);
    FindClose(hFind);
    return count;
}

uint32_t FindFilesByExtension(const Core::Path& dir, const char* ext,
    Core::Path* outPaths, uint32_t maxPaths)
{
    return FindFilesByExtensionImpl(dir, ext, outPaths, maxPaths, 0);
}
} // Platform
