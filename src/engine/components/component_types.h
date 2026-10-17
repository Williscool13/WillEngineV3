//
// Created by William on 2026-03-03.
//

#ifndef WILL_ENGINE_COMPONENT_TYPES_H
#define WILL_ENGINE_COMPONENT_TYPES_H
#include <array>
#include <concepts>

#include "core/string_id.h"


namespace Engine
{
template<typename T>
concept NamedComponent = requires {
    { T::COMPONENT_NAME } -> std::convertible_to<const char*>;
};

template<typename T>
concept DomainComponent = NamedComponent<T> && requires {
    { T::COMPONENT_DOMAIN } -> std::convertible_to<const char*>;
};

template<DomainComponent T>
struct QualifiedComponentName
{
    static constexpr size_t DOMAIN_LENGTH = StringIdLength(T::COMPONENT_DOMAIN);
    static constexpr size_t NAME_LENGTH = StringIdLength(T::COMPONENT_NAME);
    static constexpr std::array<char, DOMAIN_LENGTH + NAME_LENGTH + 2> VALUE = [] {
        std::array<char, DOMAIN_LENGTH + NAME_LENGTH + 2> s{};
        for (size_t i = 0; i < DOMAIN_LENGTH; ++i) { s[i] = T::COMPONENT_DOMAIN[i]; }
        s[DOMAIN_LENGTH] = '.';
        for (size_t i = 0; i < NAME_LENGTH; ++i) { s[DOMAIN_LENGTH + 1 + i] = T::COMPONENT_NAME[i]; }
        return s;
    }();
};

template<NamedComponent T>
constexpr const char* ComponentName()
{
    if constexpr (DomainComponent<T>) {
        return QualifiedComponentName<T>::VALUE.data();
    }
    else {
        return T::COMPONENT_NAME;
    }
}

template<NamedComponent T>
constexpr StringID TypeSID() {
    return StringID(Hash(ComponentName<T>(), StringIdLength(ComponentName<T>())));
}
}


#endif //WILL_ENGINE_COMPONENT_TYPES_H
