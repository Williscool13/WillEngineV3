//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_CONSTANTS_H
#define WILL_ENGINE_CONSTANTS_H

#include <glm/glm.hpp>

namespace Core::Math
{
inline constexpr auto WORLD_UP = glm::vec3(0.0f, 1.0f, 0.0f);
inline constexpr auto WORLD_FORWARD = glm::vec3(0.0f, 0.0f, -1.0f);
inline constexpr auto WORLD_RIGHT = glm::vec3(1.0f, 0.0f, 0.0f);

inline constexpr float SPEED_OF_LIGHT = 299792458.0f; // m/s
inline constexpr float SPEED_OF_SOUND = 343.0f; // m/s at 20°C

inline constexpr float PI = 3.14159265359f;
inline constexpr float TAU = 6.28318530718f;

/** Metres: "moved" checks, authored sizes treated as zero. */
inline constexpr float POSITION_EPSILON = 1e-4f;
/** Metres: coincident or degenerate generated geometry. */
inline constexpr float GEOMETRY_EPSILON = 1e-5f;
/** Too short to normalize a vector built from unit vectors. */
inline constexpr float DIRECTION_EPSILON = 1e-4f;
/** Zero guard before dividing. */
inline constexpr float DIVISION_EPSILON = 1e-6f;
}

using Core::Math::WORLD_UP;
using Core::Math::WORLD_FORWARD;
using Core::Math::WORLD_RIGHT;
using Core::Math::SPEED_OF_LIGHT;
using Core::Math::SPEED_OF_SOUND;
using Core::Math::PI;
using Core::Math::TAU;
using Core::Math::POSITION_EPSILON;
using Core::Math::GEOMETRY_EPSILON;
using Core::Math::DIRECTION_EPSILON;
using Core::Math::DIVISION_EPSILON;

#endif //WILL_ENGINE_CONSTANTS_H
