//
// Created by William on 2026-04-01.
//

#include "file_utils.h"

#include <cstring>
#include <Windows.h>

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
    // todo implement
}

bool FileCopy(const char* src, const char* dst)
{
    return CopyFileA(src, dst, FALSE) != FALSE;
}
} // Platform
