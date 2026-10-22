//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_BODY_USER_DATA_H
#define WILL_ENGINE_BODY_USER_DATA_H

#include <cstdint>

/**
 * Layout of a Jolt body's 64-bit user data. Bits 0-31 hold the owner (an opaque id set at creation) and bit 63 marks that an owner is set;
 * Jolt returns 0 for a body that no longer exists, which reads as no owner. Bits 32-62 are free.
 */
namespace Physics::BodyUserData
{
inline constexpr uint64_t OWNER_FLAG = 1ull << 63;
inline constexpr uint64_t OWNER_MASK = 0xFFFFFFFFull;

inline constexpr uint64_t FromOwner(uint32_t owner) { return OWNER_FLAG | owner; }
inline constexpr bool HasOwner(uint64_t data) { return (data & OWNER_FLAG) != 0; }
inline constexpr uint32_t Owner(uint64_t data) { return static_cast<uint32_t>(data & OWNER_MASK); }
} // Physics::BodyUserData

#endif //WILL_ENGINE_BODY_USER_DATA_H
