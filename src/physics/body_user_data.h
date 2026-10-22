//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_BODY_USER_DATA_H
#define WILL_ENGINE_BODY_USER_DATA_H

#include <cstdint>

/**
 * Layout of a Jolt body's 64-bit user data. Bits 0-31 hold the owner (an opaque id set at creation), bit 63 marks that an owner is set,
 * and bits 32-62 hold the conveyor slot + 1 (0 = not a conveyor). Jolt returns 0 for a body that no longer exists, which reads as no owner.
 */
namespace Physics::BodyUserData
{
inline constexpr uint64_t OWNER_FLAG = 1ull << 63;
inline constexpr uint64_t OWNER_MASK = 0xFFFFFFFFull;
inline constexpr uint32_t CONVEYOR_SHIFT = 32;
inline constexpr uint64_t CONVEYOR_MASK = 0x7FFFFFFFull << CONVEYOR_SHIFT;

inline constexpr uint64_t FromOwner(uint32_t owner) { return OWNER_FLAG | owner; }
inline constexpr bool HasOwner(uint64_t data) { return (data & OWNER_FLAG) != 0; }
inline constexpr uint32_t Owner(uint64_t data) { return static_cast<uint32_t>(data & OWNER_MASK); }

inline constexpr uint32_t ConveyorSlot(uint64_t data) { return static_cast<uint32_t>((data & CONVEYOR_MASK) >> CONVEYOR_SHIFT); }
inline constexpr uint64_t WithConveyorSlot(uint64_t data, uint32_t slot) { return (data & ~CONVEYOR_MASK) | (static_cast<uint64_t>(slot) << CONVEYOR_SHIFT & CONVEYOR_MASK); }
} // Physics::BodyUserData

#endif //WILL_ENGINE_BODY_USER_DATA_H
