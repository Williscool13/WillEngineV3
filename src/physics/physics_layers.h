//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_PHYSICS_LAYERS_H
#define WILL_ENGINE_PHYSICS_LAYERS_H

#include <cstdint>

namespace Physics::Layers
{
inline constexpr uint16_t NON_MOVING = 0;
inline constexpr uint16_t MOVING = 1;
inline constexpr uint16_t PLAYER = 2;
inline constexpr uint16_t SENSOR = 3;
inline constexpr uint16_t DEBRIS = 4;
/** A moving body that DEBRIS passes through, such as a crusher head coming down through its own shards. */
inline constexpr uint16_t IGNORE_DEBRIS = 5;
/** A moving body that CART_WALL keeps on course, such as a released lift cart. Otherwise behaves like MOVING. */
inline constexpr uint16_t CART = 6;
/** A static, usually invisible, wall that only CART bodies collide with. */
inline constexpr uint16_t CART_WALL = 7;
inline constexpr uint16_t NUM_LAYERS = 8;

/** For layer masks: a query given Bit(PLAYER) | Bit(SENSOR) skips both. */
constexpr uint32_t Bit(uint16_t layer) { return 1u << layer; }
} // Physics::Layers

#endif //WILL_ENGINE_PHYSICS_LAYERS_H
