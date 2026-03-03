//
// Created by William on 2026-03-03.
//

#ifndef WILL_ENGINE_COMPONENT_TYPES_H
#define WILL_ENGINE_COMPONENT_TYPES_H
#include "core/string_id.h"


namespace Game
{
struct ComponentEditorResult {
    bool requestRemoval{false};
};

template<typename T>
StringID TypeSID() {
#if defined(_MSC_VER)
    constexpr auto sig = __FUNCSIG__;
#elif defined(__clang__)
    constexpr auto sig = __PRETTY_FUNCTION__;
#elif defined(__GNUC__)
    constexpr auto sig = __PRETTY_FUNCTION__;
#else
#error "Unsupported compiler"
#endif
    return StringID(sig, strlen(sig));
}
}


#endif //WILL_ENGINE_COMPONENT_TYPES_H