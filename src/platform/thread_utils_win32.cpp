//
// Created by William on 2025-12-24.
//

#include "thread_utils.h"

#include <Windows.h>

namespace Platform
{
void SetThreadName(const char* name) {
    wchar_t wname[256];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 256);
    SetThreadDescription(GetCurrentThread(), wname);
}
} // Platform
