//
// Created by William on 2026-07-03.
//

#include "spline_frames.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine
{
bool SampleSplineFrames(const Spline& spline, int segmentsPerSpan, float rollAngle, Core::Vector<SplineFrame>& out)
{
    out.Clear();

    const int N = static_cast<int>(spline.points.Size());
    if (N < 2) { return false; }

    const bool bClosed = spline.bClosed;
    const int segs = std::max(1, segmentsPerSpan);
    const int totalSpans = bClosed ? N : N - 1;
    const int totalRings = bClosed ? totalSpans * segs : totalSpans * segs + 1;

    auto catmullScalar = [](float v0, float v1, float v2, float v3, float t) -> float {
        return 0.5f * ((2.0f * v1) + (-v0 + v2) * t + (2.0f * v0 - 5.0f * v1 + 4.0f * v2 - v3) * (t * t) + (-v0 + 3.0f * v1 - 3.0f * v2 + v3) * (t * t * t));
    };

    auto getRollCP = [&](int i) -> float {
        const int nR = static_cast<int>(spline.rolls.Size());
        if (nR == 0) { return 0.0f; }
        if (bClosed) { return spline.rolls[((i % nR) + nR) % nR]; }
        return spline.rolls[std::clamp(i, 0, nR - 1)];
    };

    out.Resize(static_cast<size_t>(totalRings));

    for (int i = 0; i < totalRings; i++) {
        int span;
        float t;
        if (!bClosed && i == totalRings - 1) {
            span = N - 2;
            t = 1.0f;
        }
        else {
            span = i / segs;
            t = static_cast<float>(i % segs) / static_cast<float>(segs);
        }

        const int nextSpan = bClosed ? (span + 1) % N : span + 1;
        glm::vec3 tang = spline.EvaluateTangent(span, nextSpan, t);
        if (glm::length(tang) < 1e-6f) { tang = i == 0 ? glm::vec3{0, 0, 1} : out[i - 1].tangent; }
        float roll;
        if (spline.mode == SplineMode::Linear) {
            roll = glm::mix(getRollCP(span), getRollCP(nextSpan), t);
        }
        else {
            roll = catmullScalar(getRollCP(span - 1), getRollCP(span), getRollCP(nextSpan), getRollCP(nextSpan + 1), t);
        }
        out[i].pos = spline.EvaluatePosition(span, nextSpan, t);
        out[i].tangent = glm::normalize(tang);
        out[i].roll = roll;
    }

    // Parallel transport frames
    {
        glm::vec3 worldUp = {0, 1, 0};
        if (glm::abs(glm::dot(out[0].tangent, worldUp)) > 0.99f) { worldUp = {1, 0, 0}; }
        out[0].right = glm::normalize(glm::cross(worldUp, out[0].tangent));
        out[0].up = glm::normalize(glm::cross(out[0].tangent, out[0].right));
        for (int i = 1; i < totalRings; i++) {
            glm::vec3 axis = glm::cross(out[i - 1].tangent, out[i].tangent);
            float axisLen = glm::length(axis);
            if (axisLen < 1e-6f) {
                out[i].right = out[i - 1].right;
            }
            else {
                float cosA = glm::clamp(glm::dot(out[i - 1].tangent, out[i].tangent), -1.0f, 1.0f);
                glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::acos(cosA), axis / axisLen);
                out[i].right = glm::normalize(glm::vec3(rot * glm::vec4(out[i - 1].right, 0.0f)));
            }
            out[i].up = glm::normalize(glm::cross(out[i].tangent, out[i].right));
        }
    }

    // Apply per-ring roll (base rollAngle + interpolated per-point roll)
    for (int i = 0; i < totalRings; i++) {
        float totalRoll = glm::radians(rollAngle + out[i].roll);
        float cr = glm::cos(totalRoll), sr = glm::sin(totalRoll);
        out[i].rRight = cr * out[i].right + sr * out[i].up;
        out[i].rUp = -sr * out[i].right + cr * out[i].up;
    }

    return true;
}
} // Engine
