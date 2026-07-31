//
// Created by William on 2026-07-03.
//

#include "collider_generation.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

#include "core/containers/inline_vector.h"
#include "engine/resources/font/font.h"
#include "engine/resources/font/text3d_layout.h"

namespace Engine
{
static Quat BasisToQuat(const Vec3& x, const Vec3& y, const Vec3& z)
{
    return glm::normalize(glm::quat_cast(Mat3(x, y, z)));
}

/** Orthonormal box basis whose local Z follows `dir`, local X near `rRight`, local Y near `rUp`. */
static Quat SegmentBoxRotation(const Vec3& dir, const Vec3& rRight, const Vec3& rUp)
{
    Vec3 z = dir;
    Vec3 x = rRight - z * glm::dot(rRight, z);
    if (glm::length(x) < 1e-4f) {
        x = rUp - z * glm::dot(rUp, z);
    }
    if (glm::length(x) < 1e-4f) {
        return Quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    x = glm::normalize(x);
    Vec3 y = glm::normalize(glm::cross(z, x));
    x = glm::cross(y, z);
    return BasisToQuat(x, y, z);
}

void BuildSplineColliderPrimitives(const SplineParams& p, Core::Span<const SplineFrame> frames, Core::Vector<SplineColliderPrimitive>& out)
{
    const int totalRings = static_cast<int>(frames.Size());
    if (totalRings < 2) { return; }

    const bool bClosed = p.spline.bClosed;
    const bool bTube = p.profile.type == SplineProfileType::Tube;
    const float halfW = bTube ? p.radius : std::max(0.001f, p.profile.width * 0.5f);
    const float halfH = bTube ? p.radius : std::max(0.001f, p.profile.height * 0.5f);

    const Vec3 worldUp{0.0f, 1.0f, 0.0f};
    auto horizAxes = [&](const SplineFrame& f, Vec3& across, Vec3& fwd) {
        fwd = f.tangent - worldUp * glm::dot(f.tangent, worldUp);
        if (glm::length(fwd) < 1e-4f) { fwd = f.rRight - worldUp * glm::dot(f.rRight, worldUp); }
        fwd = (glm::length(fwd) < 1e-4f) ? Vec3{0.0f, 0.0f, 1.0f} : glm::normalize(fwd);
        across = glm::normalize(glm::cross(worldUp, fwd));
    };

    auto laneCenter = [&](const SplineFrame& f, Vec2 off) -> Vec3 {
        if (p.railing.bEnabled) {
            Vec3 across, fwd;
            horizAxes(f, across, fwd);
            return f.pos + (off.x + p.railing.lateralOffset) * across + off.y * worldUp;
        }
        return f.pos + off.x * f.rRight + off.y * f.rUp;
    };

    Core::InlineVector<Vec2, 8> laneOffsets{};
    if (p.railing.bEnabled) {
        for (int li = 0; li < static_cast<int>(p.railing.lanes.Size()); li++) { laneOffsets.PushBack(p.railing.lanes[li]); }
    }
    else {
        laneOffsets.PushBack(Vec2{0.0f, 0.0f});
    }

    // Main sweep: one box (or capsule for Tube) per ring-to-ring segment, per lane.
    const int stitchCount = bClosed ? totalRings : totalRings - 1;
    for (int rail = 0; rail < static_cast<int>(laneOffsets.Size()); rail++) {
        const Vec2 laneOff = laneOffsets[rail];
        for (int i = 0; i < stitchCount; i++) {
            const int nextRing = bClosed ? (i + 1) % totalRings : i + 1;
            const Vec3 c0 = laneCenter(frames[i], laneOff);
            const Vec3 c1 = laneCenter(frames[nextRing], laneOff);
            const Vec3 seg = c1 - c0;
            const float len = glm::length(seg);
            if (len < 1e-5f) { continue; }
            const Vec3 dir = seg / len;
            const Vec3 mid = (c0 + c1) * 0.5f;

            SplineColliderPrimitive prim{};
            prim.position = mid;
            if (bTube) {
                prim.type = SplineColliderPrimitiveType::Capsule;
                prim.radius = p.radius;
                prim.halfHeight = len * 0.5f;
                // Capsule axis is local Y; orient Y along the segment.
                Vec3 y = dir;
                Vec3 x = frames[i].rRight - y * glm::dot(frames[i].rRight, y);
                if (glm::length(x) < 1e-4f) { x = frames[i].rUp - y * glm::dot(frames[i].rUp, y); }
                x = glm::length(x) < 1e-4f ? Vec3{1.0f, 0.0f, 0.0f} : glm::normalize(x);
                const Vec3 z = glm::normalize(glm::cross(x, y));
                x = glm::cross(y, z);
                prim.rotation = BasisToQuat(x, y, z);
            }
            else {
                prim.type = SplineColliderPrimitiveType::Box;
                prim.halfExtents = Vec3(halfW, halfH, len * 0.5f);
                prim.rotation = SegmentBoxRotation(dir, frames[i].rRight, frames[i].rUp);
            }
            out.PushBack(prim);
        }
    }

    // Cross planks bridge exactly two lanes.
    if (p.railing.bEnabled && p.railing.lanes.Size() == 2 && p.bCrossPlanks) {
        const float plankThickness = std::max(0.001f, p.crossPlankThickness);
        const float halfLength = std::max(0.001f, p.crossPlankLength) * 0.5f;
        const int plankInterval = std::max(1, p.crossPlankInterval);
        const Vec2 laneA = p.railing.lanes[0];
        const Vec2 laneB = p.railing.lanes[1];

        const int plankMaxRing = bClosed ? totalRings : totalRings - 1;
        for (int i = 0; i < plankMaxRing; i += plankInterval) {
            Vec3 across, fwd;
            horizAxes(frames[i], across, fwd);
            const Vec3 centerA = laneCenter(frames[i], laneA) + p.crossPlankHeight * worldUp;
            const Vec3 centerB = laneCenter(frames[i], laneB) + p.crossPlankHeight * worldUp;
            const Vec3 acrossVec = centerB - centerA;
            const float acrossLen = glm::length(acrossVec);
            if (acrossLen < 1e-5f) { continue; }
            const Vec3 xAxis = acrossVec / acrossLen; // plank long axis (lane A -> lane B)
            Vec3 zAxis = fwd - xAxis * glm::dot(fwd, xAxis);
            zAxis = glm::length(zAxis) < 1e-4f ? fwd : glm::normalize(zAxis);
            const Vec3 yAxis = glm::normalize(glm::cross(zAxis, xAxis)); // thickness (~world up)

            SplineColliderPrimitive prim{};
            prim.type = SplineColliderPrimitiveType::Box;
            prim.position = (centerA + centerB) * 0.5f - plankThickness * 0.5f * yAxis;
            prim.halfExtents = Vec3(acrossLen * 0.5f, plankThickness * 0.5f, halfLength);
            prim.rotation = BasisToQuat(xAxis, yAxis, zAxis);
            out.PushBack(prim);
        }
    }

    // Vertical posts (balusters), plumb along world up.
    if (p.railing.bEnabled && p.railing.bPosts) {
        const int postInterval = std::max(1, p.railing.postInterval);
        const float pb = p.railing.postBottom;
        const float pt = p.railing.postTop;
        const float railWidth = bTube ? 2.0f * p.radius : std::max(p.profile.width, p.profile.height);
        const float hw = std::max(0.001f, std::min(p.railing.postSize.x, railWidth)) * 0.5f;
        const float hd = std::max(0.001f, std::min(p.railing.postSize.y, railWidth)) * 0.5f;

        for (int i = 0; i < totalRings; i += postInterval) {
            const SplineFrame& f = frames[i];
            Vec3 u, w;
            horizAxes(f, u, w);
            Vec3 postBase = f.pos + (p.railing.lateralOffset + p.railing.postLateral) * u;
            if (!bClosed && totalRings > 1) {
                if (i == 0) { postBase += hd * glm::normalize(frames[1].pos - frames[0].pos); }
                else if (i == totalRings - 1) { postBase += hd * glm::normalize(frames[totalRings - 2].pos - frames[totalRings - 1].pos); }
            }
            const float halfY = std::max(0.001f, std::abs(pt - pb) * 0.5f);

            SplineColliderPrimitive prim{};
            prim.type = SplineColliderPrimitiveType::Box;
            prim.position = postBase + (pb + pt) * 0.5f * worldUp;
            prim.halfExtents = Vec3(hw, halfY, hd);
            prim.rotation = BasisToQuat(u, worldUp, w);
            out.PushBack(prim);
        }
    }
}

static void HullWedge(const WedgeParams& p, Core::Vector<Vec3>& out)
{
    // Corner pivot (matches GenerateWedge): bottom quad + top back edge.
    const float sx = p.sizeX, sy = p.sizeY, sz = p.sizeZ;
    out.PushBack(Vec3(0.0f, 0.0f, 0.0f));
    out.PushBack(Vec3(sx, 0.0f, 0.0f));
    out.PushBack(Vec3(sx, 0.0f, sz));
    out.PushBack(Vec3(0.0f, 0.0f, sz));
    out.PushBack(Vec3(0.0f, sy, sz));
    out.PushBack(Vec3(sx, sy, sz));
}

static void HullRing(float y, float radius, int slices, Core::Vector<Vec3>& out)
{
    const float twoPi = 2.0f * glm::pi<float>();
    for (int j = 0; j < slices; j++) {
        const float a = static_cast<float>(j) / static_cast<float>(slices) * twoPi;
        out.PushBack(Vec3(std::cos(a) * radius, y, std::sin(a) * radius));
    }
}

static void HullCone(const ConeParams& p, Core::Vector<Vec3>& out)
{
    // Base at y=0, apex at y=height (matches GenerateCone).
    const int slices = glm::clamp(p.slices, 3, 32);
    HullRing(0.0f, p.radius, slices, out);
    out.PushBack(Vec3(0.0f, p.height, 0.0f));
}

static void HullHemisphere(const HemisphereParams& p, Core::Vector<Vec3>& out)
{
    // Dome base at y=0 (radius r), pole at y=r (matches GenerateHemisphere).
    const int slices = glm::clamp(p.slices, 3, 24);
    const float r = p.radius;
    const int stacks = 3;
    for (int i = 0; i < stacks; i++) {
        const float phi = glm::pi<float>() * 0.5f * static_cast<float>(i) / static_cast<float>(stacks); // 0 (base) -> pi/2 (near pole)
        HullRing(r * std::sin(phi), r * std::cos(phi), slices, out);
    }
    out.PushBack(Vec3(0.0f, r, 0.0f));
}

static void PushBoxPrim(const Vec3& center, const Vec3& halfExtents, const Quat& rotation, Core::Vector<SplineColliderPrimitive>& out)
{
    SplineColliderPrimitive prim{};
    prim.type = SplineColliderPrimitiveType::Box;
    prim.position = center;
    prim.halfExtents = halfExtents;
    prim.rotation = rotation;
    out.PushBack(prim);
}

static void PushCapsulePrim(const Vec3& a, const Vec3& b, float radius, Core::Vector<SplineColliderPrimitive>& out)
{
    const Vec3 seg = b - a;
    const float len = glm::length(seg);
    if (len < 1e-6f) { return; }
    const Vec3 y = seg / len; // capsule axis is local Y
    const Vec3 ref = (std::abs(y.x) < 0.9f) ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 0.0f, 1.0f);
    const Vec3 x = glm::normalize(glm::cross(ref, y));
    const Vec3 z = glm::cross(x, y);

    SplineColliderPrimitive prim{};
    prim.type = SplineColliderPrimitiveType::Capsule;
    prim.position = (a + b) * 0.5f;
    prim.radius = radius;
    prim.halfHeight = len * 0.5f;
    prim.rotation = BasisToQuat(x, y, z);
    out.PushBack(prim);
}

// Box-per-step (X-extruded, corner pivot); mirrors par_shapes_create_staircase.
static void CompoundStaircase(const StaircaseParams& p, Core::Vector<SplineColliderPrimitive>& out)
{
    const int steps = p.stepCount;
    if (steps < 1) { return; }
    const float stepDepth = p.totalDepth / static_cast<float>(steps);
    const float uniformH = p.totalHeight / static_cast<float>(steps);
    const float usedStepH = (p.bSpecifyStepHeight && p.stepHeight > 0.0f) ? p.stepHeight : uniformH;
    const float hw = p.width * 0.5f;
    for (int i = 0; i < steps; i++) {
        const float yTop = (i < steps - 1) ? static_cast<float>(i + 1) * usedStepH : p.totalHeight;
        const float zMin = static_cast<float>(i) * stepDepth;
        PushBoxPrim(Vec3(hw, yTop * 0.5f, zMin + stepDepth * 0.5f), Vec3(hw, yTop * 0.5f, stepDepth * 0.5f), Quat(1.0f, 0.0f, 0.0f, 0.0f), out);
    }
}

static void PushHullPrim(Core::Span<const Vec3> verts, Core::Vector<SplineColliderPrimitive>& outPrims, Core::Vector<Vec3>& outPositions)
{
    SplineColliderPrimitive prim{};
    prim.type = SplineColliderPrimitiveType::ConvexHull;
    prim.hullOffset = static_cast<uint32_t>(outPositions.Size());
    prim.hullCount = static_cast<uint32_t>(verts.Size());
    for (size_t i = 0; i < verts.Size(); i++) { outPositions.PushBack(verts[i]); }
    outPrims.PushBack(prim);
}

// One ConvexHull per tread (an annular sector: narrow inner, wide outer); mirrors GenerateSpiralStaircase (center pivot, XZ helix around Y). Plus optional cylinder column.
static void CompoundSpiralStaircase(const SpiralStaircaseParams& p, Core::Vector<SplineColliderPrimitive>& out, Core::Vector<Vec3>& outPositions)
{
    const int steps = p.stepCount;
    if (steps <= 0) { return; }
    const int seg = std::max(1, p.arcSegments);
    const float ro = std::max(0.05f, p.outerRadius);
    const float ri = glm::clamp(p.centerColumnRadius, 0.001f, ro - 0.01f);
    const float stepH = std::max(0.001f, p.bSpecifyStepHeight ? p.stepHeight : p.totalHeight / static_cast<float>(steps));
    const float thick = glm::clamp(p.treadThickness, 0.001f, stepH);
    const float dStep = glm::radians(p.bSpecifyDegreesPerStep ? p.degreesPerStep : p.totalSweep / static_cast<float>(steps));
    const float heightPerRad = stepH / std::max(dStep, 1e-6f);
    const int subdiv = seg; // one hull per arc-segment so the inner/outer edges facet along the arc, matching the render tread (ramp mode also needs this for the helicoid slope)

    for (int i = 0; i < steps; i++) {
        const float a0 = static_cast<float>(i) * dStep;
        const float ytStep = static_cast<float>(i + 1) * stepH;
        for (int s = 0; s < subdiv; s++) {
            const float as = a0 + dStep * static_cast<float>(s) / static_cast<float>(subdiv);
            const float an = a0 + dStep * static_cast<float>(s + 1) / static_cast<float>(subdiv);
            const Vec3 ds(std::cos(as), 0.0f, std::sin(as));
            const Vec3 dn(std::cos(an), 0.0f, std::sin(an));
            const float topS = p.bRamp ? as * heightPerRad : ytStep;
            const float topN = p.bRamp ? an * heightPerRad : ytStep;
            // 8 corners in box-corner order: top quad [0..3], bottom quad [4..7].
            const Vec3 corners[8] = {
                ds * ri + Vec3(0.0f, topS, 0.0f),
                ds * ro + Vec3(0.0f, topS, 0.0f),
                dn * ro + Vec3(0.0f, topN, 0.0f),
                dn * ri + Vec3(0.0f, topN, 0.0f),
                ds * ri + Vec3(0.0f, topS - thick, 0.0f),
                ds * ro + Vec3(0.0f, topS - thick, 0.0f),
                dn * ro + Vec3(0.0f, topN - thick, 0.0f),
                dn * ri + Vec3(0.0f, topN - thick, 0.0f),
            };
            PushHullPrim(Core::Span<const Vec3>(corners, 8), out, outPositions);
        }
    }

    if (p.bShowCenterColumn) {
        const float colTop = static_cast<float>(steps) * stepH;
        SplineColliderPrimitive col{};
        col.type = SplineColliderPrimitiveType::Cylinder;
        col.radius = ri;
        col.halfHeight = colTop * 0.5f;
        col.position = Vec3(0.0f, colTop * 0.5f, 0.0f);
        out.PushBack(col);
    }
}

// Ring of boxes approximating the annular wall; mirrors GeneratePipe (centered Y).
static void CompoundPipe(const PipeParams& p, Core::Vector<SplineColliderPrimitive>& out)
{
    const int N = glm::clamp(p.slices, 3, 32);
    const float ro = std::max(0.01f, p.outerRadius);
    const float ri = std::max(0.001f, std::min(p.innerRadius, ro - 0.001f));
    const float hh = p.height * 0.5f;
    const float midR = (ri + ro) * 0.5f;
    const float twoPi = 2.0f * glm::pi<float>();
    const Vec3 up(0.0f, 1.0f, 0.0f);
    for (int j = 0; j < N; j++) {
        const float a0 = twoPi * static_cast<float>(j) / static_cast<float>(N);
        const float a1 = twoPi * static_cast<float>(j + 1) / static_cast<float>(N);
        const float am = (a0 + a1) * 0.5f;
        const Vec3 radial(std::cos(am), 0.0f, std::sin(am));
        const Vec3 tangent(-std::sin(am), 0.0f, std::cos(am));
        const Vec3 center(midR * std::cos(am), 0.0f, midR * std::sin(am));
        const float halfTang = std::max(0.001f, midR * std::sin((a1 - a0) * 0.5f));
        PushBoxPrim(center, Vec3((ro - ri) * 0.5f, hh, halfTang), BasisToQuat(radial, up, tangent), out);
    }
}

// Ring of capsules following the main ring; mirrors GenerateTorus (main circle in XY plane, tube radius = ringRadius*clamp(tube/ring,0.1,0.99)).
// par_shapes maps `stacks` to the main ring (theta) and `slices` to the tube cross-section, so the ring resolution comes from stacks.
static void CompoundTorus(const TorusParams& p, Core::Vector<SplineColliderPrimitive>& out)
{
    const int N = glm::clamp(p.stacks, 3, 48);
    const float rr = std::max(0.01f, p.ringRadius);
    const float tr = std::max(0.001f, p.tubeRadius);
    const float tube = rr * glm::clamp(tr / rr, 0.1f, 0.99f);
    const float twoPi = 2.0f * glm::pi<float>();
    for (int j = 0; j < N; j++) {
        const float t0 = twoPi * static_cast<float>(j) / static_cast<float>(N);
        const float t1 = twoPi * static_cast<float>(j + 1) / static_cast<float>(N);
        PushCapsulePrim(Vec3(std::cos(t0) * rr, std::sin(t0) * rr, 0.0f), Vec3(std::cos(t1) * rr, std::sin(t1) * rr, 0.0f), tube, out);
    }
}

// One ConvexHull wedge (annular sector) per slice; mirrors GenerateRing (thin slab at y=0). Wedges tile the annulus exactly (no gaps/overlap, unlike boxes). A solid disc (innerRadius=0) collapses to one thin cylinder.
static void CompoundRing(const RingParams& p, Core::Vector<SplineColliderPrimitive>& out, Core::Vector<Vec3>& outPositions)
{
    const float ro = std::max(0.01f, p.outerRadius);
    const float ri = glm::clamp(p.innerRadius, 0.0f, ro - 0.001f);
    const float hh = 0.02f;
    if (ri <= 1e-4f) {
        SplineColliderPrimitive col{};
        col.type = SplineColliderPrimitiveType::Cylinder;
        col.radius = ro;
        col.halfHeight = hh;
        out.PushBack(col);
        return;
    }
    const int N = glm::clamp(p.slices, 3, 64);
    const float twoPi = 2.0f * glm::pi<float>();
    for (int j = 0; j < N; j++) {
        const float a0 = twoPi * static_cast<float>(j) / static_cast<float>(N);
        const float a1 = twoPi * static_cast<float>(j + 1) / static_cast<float>(N);
        const Vec3 ds(std::cos(a0), 0.0f, std::sin(a0));
        const Vec3 dn(std::cos(a1), 0.0f, std::sin(a1));
        const Vec3 top(0.0f, hh, 0.0f);
        const Vec3 bot(0.0f, -hh, 0.0f);
        const Vec3 corners[8] = {
            ds * ri + top, ds * ro + top, dn * ro + top, dn * ri + top,
            ds * ri + bot, ds * ro + bot, dn * ro + bot, dn * ri + bot,
        };
        PushHullPrim(Core::Span<const Vec3>(corners, 8), out, outPositions);
    }
}

// Box-per-segment along the arch band; mirrors GenerateArch (outer/inner XY contours extruded in Z).
static void CompoundArch(const ArchParams& p, Core::Vector<SplineColliderPrimitive>& out)
{
    const int N = std::max(1, p.sides);
    const float outerR = p.width * 0.5f;
    const float innerR = std::max(0.01f, outerR - p.thickness);
    const float legH = std::max(0.0f, p.height - outerR);
    const float depth = p.depth;
    const float pi = glm::pi<float>();

    Core::InlineVector<Vec2, 80> outerC{};
    Core::InlineVector<Vec2, 80> innerC{};
    outerC.PushBack(Vec2(outerR, 0.0f));
    innerC.PushBack(Vec2(innerR, 0.0f));
    if (N == 1) {
        outerC.PushBack(Vec2(outerR, legH));
        outerC.PushBack(Vec2(outerR, legH + outerR));
        outerC.PushBack(Vec2(-outerR, legH + outerR));
        outerC.PushBack(Vec2(-outerR, legH));
        innerC.PushBack(Vec2(innerR, legH));
        innerC.PushBack(Vec2(innerR, legH + innerR));
        innerC.PushBack(Vec2(-innerR, legH + innerR));
        innerC.PushBack(Vec2(-innerR, legH));
    }
    else {
        for (int i = 0; i <= N; i++) {
            const float th = pi * static_cast<float>(i) / static_cast<float>(N);
            outerC.PushBack(Vec2(outerR * std::cos(th), legH + outerR * std::sin(th)));
            innerC.PushBack(Vec2(innerR * std::cos(th), legH + innerR * std::sin(th)));
        }
    }
    outerC.PushBack(Vec2(-outerR, 0.0f));
    innerC.PushBack(Vec2(-innerR, 0.0f));

    const int M = static_cast<int>(outerC.Size());
    const Vec3 zAxis(0.0f, 0.0f, 1.0f);
    for (int i = 0; i + 1 < M; i++) {
        const Vec2 mo = (outerC[i] + outerC[i + 1]) * 0.5f;
        const Vec2 mi = (innerC[i] + innerC[i + 1]) * 0.5f;
        const Vec2 center2 = (mo + mi) * 0.5f;
        const Vec2 radial2 = mo - mi;
        const float radialLen = glm::length(radial2);
        const Vec2 tang2 = ((outerC[i + 1] + innerC[i + 1]) - (outerC[i] + innerC[i])) * 0.5f;
        const float tangLen = glm::length(tang2);
        if (radialLen < 1e-5f || tangLen < 1e-5f) { continue; }
        const Vec3 xAxis(glm::normalize(radial2), 0.0f);
        const Vec3 yAxis = glm::cross(zAxis, xAxis); // in-plane, perpendicular to radial (unit)
        PushBoxPrim(Vec3(center2.x, center2.y, depth * 0.5f), Vec3(radialLen * 0.5f, tangLen * 0.5f, depth * 0.5f), BasisToQuat(xAxis, yAxis, zAxis), out);
    }

    if (p.bFillCorners && N >= 2) {
        const Vec2 cornerR(outerR, legH + outerR);
        const Vec2 cornerL(-outerR, legH + outerR);
        const auto addCornerBox = [&](const Vec2& A, const Vec2& B, const Vec2& corner) {
            const Vec2 segMid = (A + B) * 0.5f;
            const Vec2 center2 = (segMid + corner) * 0.5f;
            const Vec2 radial2 = corner - segMid;
            const float radialLen = glm::length(radial2);
            const Vec2 tang2 = B - A;
            const float tangLen = glm::length(tang2);
            if (radialLen < 1e-5f || tangLen < 1e-5f) { return; }
            const Vec3 xAxis(glm::normalize(radial2), 0.0f);
            const Vec3 yAxis = glm::cross(zAxis, xAxis);
            PushBoxPrim(Vec3(center2.x, center2.y, depth * 0.5f), Vec3(radialLen * 0.5f, tangLen * 0.5f, depth * 0.5f), BasisToQuat(xAxis, yAxis, zAxis), out);
        };
        for (int i = 0; i < N; i++) {
            const float th0 = pi * static_cast<float>(i) / static_cast<float>(N);
            const float th1 = pi * static_cast<float>(i + 1) / static_cast<float>(N);
            const Vec2 A(outerR * std::cos(th0), legH + outerR * std::sin(th0));
            const Vec2 B(outerR * std::cos(th1), legH + outerR * std::sin(th1));
            if (A.x >= 0.0f && B.x >= 0.0f) {
                addCornerBox(A, B, cornerR);
            }
            else if (A.x <= 0.0f && B.x <= 0.0f) {
                addCornerBox(A, B, cornerL);
            }
            else {
                const Vec2 mid = (A + B) * 0.5f;
                addCornerBox(A, mid, cornerR);
                addCornerBox(mid, B, cornerL);
            }
        }
    }
}

// par_shapes platonic vertex tables (native coords; the generators scale by `radius`).
static const float kIcosaVerts[12 * 3] = {
    0.000f, 0.000f, 1.000f, 0.894f, 0.000f, 0.447f, 0.276f, 0.851f, 0.447f, -0.724f, 0.526f, 0.447f,
    -0.724f, -0.526f, 0.447f, 0.276f, -0.851f, 0.447f, 0.724f, 0.526f, -0.447f, -0.276f, 0.851f, -0.447f,
    -0.894f, 0.000f, -0.447f, -0.276f, -0.851f, -0.447f, 0.724f, -0.526f, -0.447f, 0.000f, 0.000f, -1.000f,
};
static const float kDodecaVerts[20 * 3] = {
    0.607f, 0.000f, 0.795f, 0.188f, 0.577f, 0.795f, -0.491f, 0.357f, 0.795f, -0.491f, -0.357f, 0.795f,
    0.188f, -0.577f, 0.795f, 0.982f, 0.000f, 0.188f, 0.304f, 0.934f, 0.188f, -0.795f, 0.577f, 0.188f,
    -0.795f, -0.577f, 0.188f, 0.304f, -0.934f, 0.188f, 0.795f, 0.577f, -0.188f, -0.304f, 0.934f, -0.188f,
    -0.982f, 0.000f, -0.188f, -0.304f, -0.934f, -0.188f, 0.795f, -0.577f, -0.188f, 0.491f, 0.357f, -0.795f,
    -0.188f, 0.577f, -0.795f, -0.607f, 0.000f, -0.795f, -0.188f, -0.577f, -0.795f, 0.491f, -0.357f, -0.795f,
};
static const float kOctaVerts[6 * 3] = {
    0.000f, 0.000f, 1.000f, 1.000f, 0.000f, 0.000f, 0.000f, 1.000f, 0.000f,
    -1.000f, 0.000f, 0.000f, 0.000f, -1.000f, 0.000f, 0.000f, 0.000f, -1.000f,
};
static const float kTetraVerts[4 * 3] = {
    0.000f, 1.333f, 0.000f, 0.943f, 0.000f, 0.000f, -0.471f, 0.000f, 0.816f, -0.471f, 0.000f, -0.816f,
};

// remapZUp mirrors the generator's Z-up -> Y-up remap {px, pz, -py}; tetra is already Y-up.
static void HullPlatonic(const float* verts, int count, float r, bool remapZUp, Core::Vector<Vec3>& out)
{
    for (int i = 0; i < count; i++) {
        const float px = verts[i * 3 + 0], py = verts[i * 3 + 1], pz = verts[i * 3 + 2];
        out.PushBack(remapZUp ? Vec3(px, pz, -py) * r : Vec3(px, py, pz) * r);
    }
}

// Extruded convex door profile (front + back rings); mirrors GenerateDoor's poly construction.
static void HullDoor(const DoorParams& p, Core::Vector<Vec3>& out)
{
    const float w = p.width, h = p.height, d = p.depth, hA = p.archHeight;
    const int sides = std::max(2, p.sides);
    if (w <= 0.0f || h <= 0.0f || d <= 0.0f || hA < 0.0f || hA >= h) { return; }

    const float archStart = h - hA;
    float arcR = 0.0f, thetaStart = 0.0f, thetaEnd = glm::pi<float>();
    Vec2 arcCen(w * 0.5f, archStart);
    if (hA > 0.0f) {
        const float arcC = (w * w * 0.25f - hA * hA) / (2.0f * hA);
        arcCen = Vec2(w * 0.5f, archStart - arcC);
        arcR = hA + arcC;
        thetaStart = std::atan2(arcC, w * 0.5f);
        thetaEnd = glm::pi<float>() - thetaStart;
    }

    Core::InlineVector<Vec2, 40> poly{};
    poly.PushBack(Vec2(0.0f, 0.0f));
    const float seamX = p.bHalf ? w * 0.5f - p.gap : w;
    poly.PushBack(Vec2(seamX, 0.0f));
    if (hA > 0.0f) {
        const float arcFrom = p.bHalf ? std::acos(glm::clamp(-p.gap / arcR, -1.0f, 1.0f)) : thetaStart;
        for (int i = 0; i <= sides; i++) {
            const float t = static_cast<float>(i) / static_cast<float>(sides);
            const float theta = arcFrom + (thetaEnd - arcFrom) * t;
            poly.PushBack(Vec2(arcCen.x + arcR * std::cos(theta), arcCen.y + arcR * std::sin(theta)));
        }
    }
    else {
        poly.PushBack(Vec2(seamX, h));
        poly.PushBack(Vec2(0.0f, h));
    }
    if (p.bFlip) {
        for (Vec2& pt : poly) { pt.x = -pt.x; }
    }
    for (const Vec2& pt : poly) {
        out.PushBack(Vec3(pt.x, pt.y, 0.0f));
        out.PushBack(Vec3(pt.x, pt.y, d));
    }
}

// Greedy maximal-rectangle cover of the solid cells in GenerateWall's cut grid
static void CompoundWall(const WallParams& p, Core::Vector<SplineColliderPrimitive>& out)
{
    const float sx = p.sizeX, sy = p.sizeY, sz = p.sizeZ;
    if (sx <= 0.0f || sy <= 0.0f || sz <= 0.0f) { return; }

    struct Rect { float x0, y0, x1, y1; };
    Rect holes[WallParams::MAX_OPENINGS];
    int holeCount = 0;
    const int32_t count = glm::clamp(p.openingCount, 0, WallParams::MAX_OPENINGS);
    for (int32_t i = 0; i < count; i++) {
        const WallOpening& o = p.openings[i];
        const Rect r{glm::clamp(o.x, 0.0f, sx), glm::clamp(o.y, 0.0f, sy), glm::clamp(o.x + o.w, 0.0f, sx), glm::clamp(o.y + o.h, 0.0f, sy)};
        if (r.x1 - r.x0 > 1e-5f && r.y1 - r.y0 > 1e-5f) { holes[holeCount++] = r; }
    }

    constexpr int MAX_CUTS = 2 + 2 * WallParams::MAX_OPENINGS;
    float xs[MAX_CUTS], ys[MAX_CUTS];
    int xn = 0, yn = 0;
    auto insertCut = [](float* arr, int& n, float v) {
        for (int i = 0; i < n; i++) {
            if (glm::abs(arr[i] - v) < 1e-5f) { return; }
            if (v < arr[i]) {
                for (int j = n; j > i; j--) { arr[j] = arr[j - 1]; }
                arr[i] = v;
                n++;
                return;
            }
        }
        arr[n++] = v;
    };
    insertCut(xs, xn, 0.0f);
    insertCut(xs, xn, sx);
    insertCut(ys, yn, 0.0f);
    insertCut(ys, yn, sy);
    for (int i = 0; i < holeCount; i++) {
        insertCut(xs, xn, holes[i].x0);
        insertCut(xs, xn, holes[i].x1);
        insertCut(ys, yn, holes[i].y0);
        insertCut(ys, yn, holes[i].y1);
    }

    const int cols = xn - 1, rows = yn - 1;
    bool solid[(MAX_CUTS - 1) * (MAX_CUTS - 1)];
    bool used[(MAX_CUTS - 1) * (MAX_CUTS - 1)] = {};
    for (int cy = 0; cy < rows; cy++) {
        for (int cx = 0; cx < cols; cx++) {
            const float mx = (xs[cx] + xs[cx + 1]) * 0.5f;
            const float my = (ys[cy] + ys[cy + 1]) * 0.5f;
            bool inHole = false;
            for (int i = 0; i < holeCount; i++) {
                if (mx > holes[i].x0 && mx < holes[i].x1 && my > holes[i].y0 && my < holes[i].y1) {
                    inHole = true;
                    break;
                }
            }
            solid[cy * cols + cx] = !inHole;
        }
    }

    for (int cy = 0; cy < rows; cy++) {
        for (int cx = 0; cx < cols; cx++) {
            if (!solid[cy * cols + cx] || used[cy * cols + cx]) { continue; }
            int cx1 = cx;
            while (cx1 + 1 < cols && solid[cy * cols + cx1 + 1] && !used[cy * cols + cx1 + 1]) { cx1++; }
            int cy1 = cy;
            bool grow = true;
            while (grow && cy1 + 1 < rows) {
                for (int x = cx; x <= cx1; x++) {
                    if (!solid[(cy1 + 1) * cols + x] || used[(cy1 + 1) * cols + x]) {
                        grow = false;
                        break;
                    }
                }
                if (grow) { cy1++; }
            }
            for (int y = cy; y <= cy1; y++) {
                for (int x = cx; x <= cx1; x++) { used[y * cols + x] = true; }
            }

            SplineColliderPrimitive prim{};
            prim.type = SplineColliderPrimitiveType::Box;
            prim.halfExtents = Vec3((xs[cx1 + 1] - xs[cx]) * 0.5f, (ys[cy1 + 1] - ys[cy]) * 0.5f, sz * 0.5f);
            prim.position = Vec3((xs[cx] + xs[cx1 + 1]) * 0.5f, (ys[cy] + ys[cy1 + 1]) * 0.5f, sz * 0.5f);
            out.PushBack(prim);
        }
    }
}

// One oriented box per member (local Y along the member), mirrors GenerateLattice's membership exactly.
static void CompoundLattice(const LatticeParams& p, Core::Vector<SplineColliderPrimitive>& out)
{
    const float sx = p.sizeX, sy = p.sizeY, sz = p.sizeZ;
    if (sx <= 0.0f || sy <= 0.0f || sz <= 0.0f) { return; }
    const int bays = glm::max(1, p.bayCount);
    const float chord = glm::clamp(p.chordSize, 0.005f, glm::min(sx, sz));
    const float brace = glm::clamp(p.braceSize, 0.005f, glm::min(sx, sz));

    auto addMember = [&](Vec3 a, Vec3 b, float t) {
        const Vec3 seg = b - a;
        const float len = glm::length(seg);
        if (len < 1e-5f) { return; }
        const Vec3 y = seg / len;
        const Vec3 ref = (std::abs(y.y) < 0.99f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
        const Vec3 x = glm::normalize(glm::cross(ref, y));
        const Vec3 z = glm::cross(x, y);
        PushBoxPrim((a + b) * 0.5f, Vec3(t * 0.5f, len * 0.5f, t * 0.5f), BasisToQuat(x, y, z), out);
    };

    const float ch = chord * 0.5f, bh = brace * 0.5f;
    const float cxs[2] = {ch, sx - ch}, czs[2] = {ch, sz - ch};
    const float bayH = sy / static_cast<float>(bays);
    auto ringY = [&](int i) { return glm::clamp(static_cast<float>(i) * bayH, bh, sy - bh); };

    for (int ix = 0; ix < 2; ix++) {
        for (int iz = 0; iz < 2; iz++) {
            addMember({cxs[ix], 0, czs[iz]}, {cxs[ix], sy, czs[iz]}, chord);
        }
    }
    for (int i = 0; i <= bays; i++) {
        const float y = ringY(i);
        addMember({cxs[0], y, czs[0]}, {cxs[1], y, czs[0]}, brace);
        addMember({cxs[0], y, czs[1]}, {cxs[1], y, czs[1]}, brace);
        addMember({cxs[0], y, czs[0]}, {cxs[0], y, czs[1]}, brace);
        addMember({cxs[1], y, czs[0]}, {cxs[1], y, czs[1]}, brace);
    }
    for (int i = 0; i < bays; i++) {
        const float y0 = ringY(i), y1 = ringY(i + 1);
        for (int face = 0; face < 4; face++) {
            Vec3 c0, c1;
            if (face < 2) {
                c0 = {cxs[0], 0, czs[face]};
                c1 = {cxs[1], 0, czs[face]};
            }
            else {
                c0 = {cxs[face - 2], 0, czs[0]};
                c1 = {cxs[face - 2], 0, czs[1]};
            }
            if (p.pattern == 0 || ((i + face) & 1) == 0) {
                addMember({c0.x, y0, c0.z}, {c1.x, y1, c1.z}, brace);
            }
            if (p.pattern == 0 || ((i + face) & 1) == 1) {
                addMember({c1.x, y0, c1.z}, {c0.x, y1, c0.z}, brace);
            }
        }
    }
}

// Web slab + one box per rib (flanks folded into the rib box), mirrors GenerateCorrugatedPanel's profile.
static void CompoundCorrugatedPanel(const CorrugatedPanelParams& p, Core::Vector<SplineColliderPrimitive>& out)
{
    const float sx = p.sizeX, sy = p.sizeY, base = p.sizeZ;
    if (sx <= 0.0f || sy <= 0.0f || base <= 0.0f) { return; }
    const int ribs = glm::max(1, p.ribCount);
    const float pitch = sx / static_cast<float>(ribs);
    const float w = glm::clamp(p.ribWidth, 0.0f, glm::max(0.0f, pitch - 2e-3f));
    const float flank = glm::min(glm::max(p.ribDepth, 0.0f), (pitch - w) * 0.5f);
    const float depth = glm::max(p.ribDepth, 0.0f);

    SplineColliderPrimitive web{};
    web.type = SplineColliderPrimitiveType::Box;
    web.halfExtents = Vec3(sx * 0.5f, sy * 0.5f, base * 0.5f);
    web.position = web.halfExtents;
    out.PushBack(web);

    if (depth <= 0.0f || w <= 0.0f) { return; }
    const float ribHalfX = w * 0.5f + flank * 0.5f;
    for (int i = 0; i < ribs; i++) {
        const float c = (static_cast<float>(i) + 0.5f) * pitch;
        SplineColliderPrimitive prim{};
        prim.type = SplineColliderPrimitiveType::Box;
        prim.halfExtents = Vec3(ribHalfX, sy * 0.5f, depth * 0.5f);
        prim.position = Vec3(c, sy * 0.5f, base + depth * 0.5f);
        out.PushBack(prim);
    }
}

bool CanBuildProceduralCollider(const ProceduralParams& params)
{
    return std::holds_alternative<BoxParams>(params)
        || std::holds_alternative<WedgeParams>(params)
        || std::holds_alternative<CylinderParams>(params)
        || std::holds_alternative<ConeParams>(params)
        || std::holds_alternative<HemisphereParams>(params)
        || std::holds_alternative<SphereParams>(params)
        || std::holds_alternative<SubdividedSphereParams>(params)
        || std::holds_alternative<CapsuleParams>(params)
        || std::holds_alternative<PlaneParams>(params)
        || std::holds_alternative<StaircaseParams>(params)
        || std::holds_alternative<SpiralStaircaseParams>(params)
        || std::holds_alternative<PipeParams>(params)
        || std::holds_alternative<TorusParams>(params)
        || std::holds_alternative<RingParams>(params)
        || std::holds_alternative<ArchParams>(params)
        || std::holds_alternative<DoorParams>(params)
        || std::holds_alternative<WallParams>(params)
        || std::holds_alternative<LatticeParams>(params)
        || std::holds_alternative<CorrugatedPanelParams>(params)
        || std::holds_alternative<TetrahedronParams>(params)
        || std::holds_alternative<OctahedronParams>(params)
        || std::holds_alternative<IcosahedronParams>(params)
        || std::holds_alternative<DodecahedronParams>(params);
}

bool BuildProceduralCollider(const ProceduralParams& params, PhysicsColliderKind& outKind, Core::Vector<SplineColliderPrimitive>& outPrimitives, Core::Vector<Vec3>& outPositions)
{
    // Shapes with an exact Jolt primitive become a single-primitive Compound (cheaper, exact inertia).
    if (const auto* p = std::get_if<BoxParams>(&params)) {
        // Corner pivot (matches GenerateBox): (0,0,0)..(size); center the box primitive.
        outKind = PhysicsColliderKind::Compound;
        SplineColliderPrimitive prim{};
        prim.type = SplineColliderPrimitiveType::Box;
        prim.halfExtents = Vec3(p->sizeX * 0.5f, p->sizeY * 0.5f, p->sizeZ * 0.5f);
        prim.position = prim.halfExtents;
        outPrimitives.PushBack(prim);
        return true;
    }
    if (const auto* p = std::get_if<PlaneParams>(&params)) {
        // Centered XZ at y=0 (matches GeneratePlane); a flat hull is degenerate, so use a thin box.
        outKind = PhysicsColliderKind::Compound;
        SplineColliderPrimitive prim{};
        prim.type = SplineColliderPrimitiveType::Box;
        prim.halfExtents = Vec3(std::max(0.001f, p->sizeX * 0.5f), 0.01f, std::max(0.001f, p->sizeZ * 0.5f));
        outPrimitives.PushBack(prim);
        return true;
    }
    if (const auto* p = std::get_if<CapsuleParams>(&params)) {
        outKind = PhysicsColliderKind::Compound;
        SplineColliderPrimitive prim{};
        prim.type = SplineColliderPrimitiveType::Capsule;
        prim.radius = p->radius;
        prim.halfHeight = std::max(0.0f, p->height * 0.5f - p->radius);
        outPrimitives.PushBack(prim);
        return true;
    }
    if (const auto* p = std::get_if<SphereParams>(&params)) {
        outKind = PhysicsColliderKind::Compound;
        SplineColliderPrimitive prim{};
        prim.type = SplineColliderPrimitiveType::Sphere;
        prim.radius = p->radius;
        outPrimitives.PushBack(prim);
        return true;
    }
    if (const auto* p = std::get_if<SubdividedSphereParams>(&params)) {
        outKind = PhysicsColliderKind::Compound;
        SplineColliderPrimitive prim{};
        prim.type = SplineColliderPrimitiveType::Sphere;
        prim.radius = p->radius;
        outPrimitives.PushBack(prim);
        return true;
    }
    if (const auto* p = std::get_if<CylinderParams>(&params)) {
        // Centered Y (matches GenerateCylinder).
        outKind = PhysicsColliderKind::Compound;
        SplineColliderPrimitive prim{};
        prim.type = SplineColliderPrimitiveType::Cylinder;
        prim.radius = p->radius;
        prim.halfHeight = p->height * 0.5f;
        outPrimitives.PushBack(prim);
        return true;
    }

    // Swept / stepped shapes become a Compound of oriented primitives following the generator's layout.
    if (const auto* p = std::get_if<StaircaseParams>(&params)) {
        outKind = PhysicsColliderKind::Compound;
        CompoundStaircase(*p, outPrimitives);
        return true;
    }
    if (const auto* p = std::get_if<SpiralStaircaseParams>(&params)) {
        outKind = PhysicsColliderKind::Compound;
        CompoundSpiralStaircase(*p, outPrimitives, outPositions);
        return true;
    }
    if (const auto* p = std::get_if<PipeParams>(&params)) {
        outKind = PhysicsColliderKind::Compound;
        CompoundPipe(*p, outPrimitives);
        return true;
    }
    if (const auto* p = std::get_if<TorusParams>(&params)) {
        outKind = PhysicsColliderKind::Compound;
        CompoundTorus(*p, outPrimitives);
        return true;
    }
    if (const auto* p = std::get_if<RingParams>(&params)) {
        outKind = PhysicsColliderKind::Compound;
        CompoundRing(*p, outPrimitives, outPositions);
        return true;
    }
    if (const auto* p = std::get_if<ArchParams>(&params)) {
        outKind = PhysicsColliderKind::Compound;
        CompoundArch(*p, outPrimitives);
        return true;
    }
    if (const auto* p = std::get_if<WallParams>(&params)) {
        outKind = PhysicsColliderKind::Compound;
        CompoundWall(*p, outPrimitives);
        return true;
    }
    if (const auto* p = std::get_if<LatticeParams>(&params)) {
        outKind = PhysicsColliderKind::Compound;
        CompoundLattice(*p, outPrimitives);
        return true;
    }
    if (const auto* p = std::get_if<CorrugatedPanelParams>(&params)) {
        outKind = PhysicsColliderKind::Compound;
        CompoundCorrugatedPanel(*p, outPrimitives);
        return true;
    }

    // Shapes with no matching Jolt primitive fall back to a ConvexHull of analytic vertices.
    if (const auto* p = std::get_if<WedgeParams>(&params)) {
        outKind = PhysicsColliderKind::ConvexHull;
        HullWedge(*p, outPositions);
        return true;
    }
    if (const auto* p = std::get_if<ConeParams>(&params)) {
        outKind = PhysicsColliderKind::ConvexHull;
        HullCone(*p, outPositions);
        return true;
    }
    if (const auto* p = std::get_if<HemisphereParams>(&params)) {
        outKind = PhysicsColliderKind::ConvexHull;
        HullHemisphere(*p, outPositions);
        return true;
    }
    if (const auto* p = std::get_if<DoorParams>(&params)) {
        outKind = PhysicsColliderKind::ConvexHull;
        HullDoor(*p, outPositions);
        return true;
    }
    if (const auto* p = std::get_if<TetrahedronParams>(&params)) {
        outKind = PhysicsColliderKind::ConvexHull;
        HullPlatonic(kTetraVerts, 4, p->radius, false, outPositions);
        return true;
    }
    if (const auto* p = std::get_if<OctahedronParams>(&params)) {
        outKind = PhysicsColliderKind::ConvexHull;
        HullPlatonic(kOctaVerts, 6, p->radius, true, outPositions);
        return true;
    }
    if (const auto* p = std::get_if<IcosahedronParams>(&params)) {
        outKind = PhysicsColliderKind::ConvexHull;
        HullPlatonic(kIcosaVerts, 12, p->radius, true, outPositions);
        return true;
    }
    if (const auto* p = std::get_if<DodecahedronParams>(&params)) {
        outKind = PhysicsColliderKind::ConvexHull;
        HullPlatonic(kDodecaVerts, 20, p->radius, true, outPositions);
        return true;
    }
    return false;
}

bool BuildText3DColliderPrimitives(const Font& font, const Text3DParams& params, Core::Vector<SplineColliderPrimitive>& out)
{
    const float scale = params.scale;
    const float halfDepth = params.depth * 0.5f;

    Text3DGlyphPlacements placements;
    LayoutText3D(font, params, placements);

    for (uint32_t p = 0; p < placements.Size(); ++p) {
        const WGlyphInfo& g = font.glyphs[placements[p].glyphIndex];
        const WGlyphContourRange& gcr = font.glyphContourRanges[placements[p].glyphIndex];
        if (gcr.contourCount == 0) { continue; }

        // Plane bounds are the glyph's outline box in the same EM space BuildText3DGeometry extrudes; the pen offset bakes in before scaling.
        const float x0 = (placements[p].penX + g.planeLeft) * scale;
        const float x1 = (placements[p].penX + g.planeRight) * scale;
        const float y0 = (placements[p].penY + g.planeBottom) * scale;
        const float y1 = (placements[p].penY + g.planeTop) * scale;
        const Vec3 he((x1 - x0) * 0.5f, (y1 - y0) * 0.5f, halfDepth);
        if (he.x > 1e-5f && he.y > 1e-5f && he.z > 1e-5f) {
            const Vec3 center((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0.0f);
            PushBoxPrim(center, he, Quat(1.0f, 0.0f, 0.0f, 0.0f), out);
        }
    }

    return !out.IsEmpty();
}
} // Engine
