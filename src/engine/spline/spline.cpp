//
// Created by William on 2026-03-27.
//

#include "spline.h"

#include <algorithm>

#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

namespace Engine
{
static glm::vec3 CatmullRomPos(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t)
{
    float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

static glm::vec3 CatmullRomTan(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t)
{
    return 0.5f * ((-p0 + p2) + 2.0f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t + 3.0f * (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * (t * t));
}

glm::vec3 Spline::EvaluatePosition(int32_t from, int32_t to, float t) const
{
    const int32_t N = static_cast<int32_t>(points.Size());
    if (N < 2) {
        return N == 1 ? points[0] : glm::vec3(0.0f);
    }

    if (mode == SplineMode::Linear) {
        return glm::mix(points[from], points[to], t);
    }

    const bool bForward = bClosed ? (to == (from + 1) % N) : (from < to);

    int32_t i0, i3;
    if (bClosed) {
        i0 = (from - 1 + N) % N;
        i3 = (to + 1) % N;
    }
    else {
        if (bForward) {
            i0 = std::max(from - 1, 0);
            i3 = std::min(to + 1, N - 1);
        }
        else {
            i0 = std::min(from + 1, N - 1);
            i3 = std::max(to - 1, 0);
        }
    }

    return CatmullRomPos(points[i0], points[from], points[to], points[i3], t);
}

glm::vec3 Spline::EvaluateTangent(int32_t from, int32_t to, float t) const
{
    const auto N = static_cast<int32_t>(points.Size());
    if (N < 2) {
        return {0.0f, 0.0f, 1.0f};
    }

    if (mode == SplineMode::Linear) {
        return points[to] - points[from];
    }

    const bool bForward = bClosed ? (to == (from + 1) % N) : (from < to);

    int32_t i0, i3;
    if (bClosed) {
        i0 = (from - 1 + N) % N;
        i3 = (to + 1) % N;
    }
    else {
        if (bForward) {
            i0 = std::max(from - 1, 0);
            i3 = std::min(to + 1, N - 1);
        }
        else {
            i0 = std::min(from + 1, N - 1);
            i3 = std::max(to - 1, 0);
        }
    }

    return CatmullRomTan(points[i0], points[from], points[to], points[i3], t);
}

int32_t Spline::SegmentCount() const
{
    return bClosed ? static_cast<int32_t>(points.Size()) : std::max(0, static_cast<int32_t>(points.Size()) - 1);
}

void Spline::Serialize(const Spline& spline, TextWriter& w)
{
    w.KeyOpt("mode", static_cast<uint32_t>(spline.mode), static_cast<uint32_t>(SplineMode::CatmullRom));
    w.KeyOpt("bClosed", spline.bClosed, false);
    if (!spline.points.IsEmpty()) {
        w.Count("points", static_cast<uint32_t>(spline.points.Size()));
        for (size_t i = 0; i < spline.points.Size(); i++) {
            w.BeginBlock("p");
            w.Key("pos", spline.points[i]);
            w.KeyOpt("roll", i < spline.rolls.Size() ? spline.rolls[i] : 0.0f, 0.0f);
            w.EndBlock();
        }
    }
}

void Spline::Deserialize(Spline& spline, const TextReader& r)
{
    spline.points.Clear();
    spline.rolls.Clear();
    r.ForEachRecord("points", [&](const TextReader& p) {
        if (spline.points.Size() >= MaxPoints) { return; }
        spline.points.PushBack(p.Vec3("pos"));
        spline.rolls.PushBack(p.Float("roll", 0.0f));
    });
    spline.mode = static_cast<SplineMode>(r.UInt("mode", static_cast<uint32_t>(SplineMode::CatmullRom)));
    spline.bClosed = r.Bool("bClosed", false);
}
}
