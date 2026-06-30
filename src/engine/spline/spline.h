//
// Created by William on 2026-03-27.
//

#ifndef WILL_ENGINE_SPLINE_H
#define WILL_ENGINE_SPLINE_H

#include <cstdint>
#include <glm/glm.hpp>
#include <json/nlohmann/json_fwd.hpp>
#include "core/containers/inline_vector.h"

namespace Engine
{
enum class SplineMode : uint8_t
{
    Linear,
    CatmullRom,
    COUNT
};

inline const char* SplineModeNames[] = {
    "Linear",
    "Catmull-Rom",
};

struct Spline
{
    static constexpr size_t MaxPoints = 64;

    Core::InlineVector<glm::vec3, MaxPoints> points;
    Core::InlineVector<float, MaxPoints> rolls; // per-point rotation around tangent (degrees)
    SplineMode mode{SplineMode::CatmullRom};
    bool bClosed{false};

    /**
     * Evaluate position on segment [from -> to] at t in [0,1]
     * @param from
     * @param to
     * @param t
     * @return
     */
    [[nodiscard]] glm::vec3 EvaluatePosition(int32_t from, int32_t to, float t) const;

    /**
     * Evaluate un-normalized tangent on segment [from -> to] at t in [0,1]
     * @param from
     * @param to
     * @param t
     * @return
     */

    [[nodiscard]] glm::vec3 EvaluateTangent(int32_t from, int32_t to, float t) const;
    //
    /**
     * Number of traversable segments (N-1 open, N closed)
     * @return
     */
    [[nodiscard]] int32_t SegmentCount() const;

    static void Serialize(const Spline& spline, nlohmann::json& json);
    static void Deserialize(Spline& spline, const nlohmann::json& json);
};
}

#endif //WILL_ENGINE_SPLINE_H
