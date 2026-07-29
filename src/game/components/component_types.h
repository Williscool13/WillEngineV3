//
// Created by William on 2026-03-03.
//

#ifndef WILL_ENGINE_COMPONENT_TYPES_H
#define WILL_ENGINE_COMPONENT_TYPES_H
#include <concepts>

#include "core/string_id.h"


namespace Game
{
template<typename T>
concept NamedComponent = requires {
    { T::COMPONENT_NAME } -> std::convertible_to<const char*>;
};

template<NamedComponent T>
StringID TypeSID() {
    return StringID(T::COMPONENT_NAME, strlen(T::COMPONENT_NAME));
}
}


#endif //WILL_ENGINE_COMPONENT_TYPES_H