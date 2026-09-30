//
// Created by William on 2026-09-30.
//

#ifndef WILL_ENGINE_CLIPBOARD_H
#define WILL_ENGINE_CLIPBOARD_H

#include <string_view>

#include "core/containers/vector.h"

namespace Platform
{
void SetClipboardOwner(void* nativeWindow);

/**
 *
 * @param text
 * @param privateFormat
 * @return
 */
bool SetClipboardText(std::string_view text, const char* privateFormat = nullptr);

/**
 *
 * @param out appended w/ contents of clipboard
 * @param privateFormat
 * @return
 */
bool GetClipboardText(Core::Vector<char>& out, const char* privateFormat = nullptr);

/**
 *
 * @param privateFormat
 * @return
 */
bool HasClipboardText(const char* privateFormat = nullptr);
}

#endif //WILL_ENGINE_CLIPBOARD_H
