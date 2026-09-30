//
// Created by William on 2026-09-30.
//

#include "clipboard.h"

#include <cstring>
#include <cwchar>
#include <Windows.h>

namespace Platform
{
static HWND gClipboardOwner = nullptr;

void SetClipboardOwner(void* nativeWindow)
{
    gClipboardOwner = static_cast<HWND>(nativeWindow);
}

static bool OpenClipboardRetrying()
{
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (OpenClipboard(gClipboardOwner)) { return true; }
        Sleep(1);
    }
    return false;
}

static HGLOBAL AllocUnicodeText(std::string_view text)
{
    const int wideLen = text.empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    size_t lineFeeds = 0;
    for (const char c : text) {
        if (c == '\n') { ++lineFeeds; }
    }
    const size_t total = static_cast<size_t>(wideLen) + lineFeeds + 1;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, total * sizeof(wchar_t));
    if (!mem) { return nullptr; }
    auto* dst = static_cast<wchar_t*>(GlobalLock(mem));
    if (wideLen > 0) { MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), dst, wideLen); }
    size_t write = total - 1;
    dst[write] = L'\0';
    for (size_t read = static_cast<size_t>(wideLen); read-- > 0;) {
        dst[--write] = dst[read];
        if (dst[read] == L'\n' && (read == 0 || dst[read - 1] != L'\r')) { dst[--write] = L'\r'; }
    }
    if (write > 0) { memmove(dst, dst + write, (total - write) * sizeof(wchar_t)); }
    GlobalUnlock(mem);
    return mem;
}

static HGLOBAL AllocBytes(std::string_view text)
{
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!mem) { return nullptr; }
    auto* dst = static_cast<char*>(GlobalLock(mem));
    memcpy(dst, text.data(), text.size());
    dst[text.size()] = '\0';
    GlobalUnlock(mem);
    return mem;
}

bool SetClipboardText(std::string_view text, const char* privateFormat)
{
    HGLOBAL unicode = AllocUnicodeText(text);
    HGLOBAL bytes = privateFormat ? AllocBytes(text) : nullptr;
    const UINT format = privateFormat ? RegisterClipboardFormatA(privateFormat) : 0;
    if (!unicode || (privateFormat && (!bytes || format == 0)) || !OpenClipboardRetrying()) {
        if (unicode) { GlobalFree(unicode); }
        if (bytes) { GlobalFree(bytes); }
        return false;
    }

    EmptyClipboard();
    bool bOk = SetClipboardData(CF_UNICODETEXT, unicode) != nullptr;
    if (!bOk) { GlobalFree(unicode); }
    if (bytes && !SetClipboardData(format, bytes)) {
        GlobalFree(bytes);
        bOk = false;
    }
    CloseClipboard();
    return bOk;
}

bool GetClipboardText(Core::Vector<char>& out, const char* privateFormat)
{
    const UINT format = privateFormat ? RegisterClipboardFormatA(privateFormat) : 0;
    if (!OpenClipboardRetrying()) { return false; }

    bool bOk = false;
    if (format != 0 && IsClipboardFormatAvailable(format)) {
        if (HANDLE mem = GetClipboardData(format)) {
            if (const auto* src = static_cast<const char*>(GlobalLock(mem))) {
                const size_t len = strnlen(src, GlobalSize(mem));
                const size_t base = out.Size();
                out.Resize(base + len);
                memcpy(out.Data() + base, src, len);
                GlobalUnlock(mem);
                bOk = true;
            }
        }
    }
    else if (HANDLE mem = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* src = static_cast<const wchar_t*>(GlobalLock(mem))) {
            const int wideLen = static_cast<int>(wcsnlen(src, GlobalSize(mem) / sizeof(wchar_t)));
            const int len = wideLen > 0 ? WideCharToMultiByte(CP_UTF8, 0, src, wideLen, nullptr, 0, nullptr, nullptr) : 0;
            const size_t base = out.Size();
            out.Resize(base + static_cast<size_t>(len));
            if (len > 0) { WideCharToMultiByte(CP_UTF8, 0, src, wideLen, out.Data() + base, len, nullptr, nullptr); }
            GlobalUnlock(mem);

            size_t write = base;
            for (size_t read = base; read < out.Size(); ++read) {
                if (out[read] == '\r' && read + 1 < out.Size() && out[read + 1] == '\n') { continue; }
                out[write++] = out[read];
            }
            out.Resize(write);
            bOk = true;
        }
    }
    CloseClipboard();
    return bOk;
}

bool HasClipboardText(const char* privateFormat)
{
    if (privateFormat && IsClipboardFormatAvailable(RegisterClipboardFormatA(privateFormat))) { return true; }
    return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
}
}
