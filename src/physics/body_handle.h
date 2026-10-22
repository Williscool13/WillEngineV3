//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_BODY_HANDLE_H
#define WILL_ENGINE_BODY_HANDLE_H

#include <cstdint>

namespace Physics
{
/** A physics body, the same bits as Jolt's BodyID; stale once the body is destroyed. */
struct BodyHandle
{
    static constexpr uint32_t INVALID = 0xFFFFFFFF;

    uint32_t value{INVALID};

    [[nodiscard]] constexpr bool IsValid() const { return value != INVALID; }
    constexpr bool operator==(const BodyHandle& other) const { return value == other.value; }
};
} // Physics

#endif //WILL_ENGINE_BODY_HANDLE_H
