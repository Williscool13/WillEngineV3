//
// Created by William on 2026-06-22.
//

#include "text3d_geometry.h"

#include <cmath>
#include <vector>

#include "earcut/earcut.hpp"
#include "engine/resources/font/font.h"
#include "engine/resources/font/text3d_layout.h"

namespace AssetLoad
{
namespace Text3DDetail
{
struct EcPoint
{
    double x{0.0};
    double y{0.0};
};

// earcut adapters: it requires lowercase size()/operator[]/value_type. Polygon ring 0 is the outer boundary, the rest holes.
struct EcRing
{
    using value_type = EcPoint;
    const EcPoint* ptr{nullptr};
    size_t n{0};
    size_t size() const { return n; }
    const EcPoint& operator[](size_t i) const { return ptr[i]; }
};

struct EcPolygon
{
    const EcRing* ptr{nullptr};
    size_t n{0};
    bool empty() const { return n == 0; }
    size_t size() const { return n; }
    const EcRing& operator[](size_t i) const { return ptr[i]; }
};

struct RingInfo
{
    uint32_t offset{0};
    uint32_t count{0};
    double area{0.0};
    bool bHole{false};
    int32_t parent{-1};
};

constexpr int MAX_BEZIER_DEPTH = 16;

static EcPoint Mid(EcPoint a, EcPoint b) { return {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5}; }

static double Cross2(EcPoint a, EcPoint b) { return a.x * b.y - a.y * b.x; }

static double LineDistance(EcPoint a, EcPoint b, EcPoint p)
{
    const EcPoint ab{b.x - a.x, b.y - a.y};
    const EcPoint ap{p.x - a.x, p.y - a.y};
    const double len2 = ab.x * ab.x + ab.y * ab.y;
    if (len2 < 1e-18) { return std::sqrt(ap.x * ap.x + ap.y * ap.y); }
    return std::abs(Cross2(ab, ap)) / std::sqrt(len2);
}

static void SubdivideQuadratic(EcPoint p0, EcPoint p1, EcPoint p2, double tol, int depth, Core::Vector<EcPoint>& out)
{
    if (depth >= MAX_BEZIER_DEPTH || LineDistance(p0, p2, p1) <= tol) { return; }
    const EcPoint p01 = Mid(p0, p1);
    const EcPoint p12 = Mid(p1, p2);
    const EcPoint m = Mid(p01, p12);
    SubdivideQuadratic(p0, p01, m, tol, depth + 1, out);
    out.PushBack(m);
    SubdivideQuadratic(m, p12, p2, tol, depth + 1, out);
}

static void SubdivideCubic(EcPoint p0, EcPoint p1, EcPoint p2, EcPoint p3, double tol, int depth, Core::Vector<EcPoint>& out)
{
    if (depth >= MAX_BEZIER_DEPTH || (LineDistance(p0, p3, p1) <= tol && LineDistance(p0, p3, p2) <= tol)) { return; }
    const EcPoint p01 = Mid(p0, p1);
    const EcPoint p12 = Mid(p1, p2);
    const EcPoint p23 = Mid(p2, p3);
    const EcPoint p012 = Mid(p01, p12);
    const EcPoint p123 = Mid(p12, p23);
    const EcPoint m = Mid(p012, p123);
    SubdivideCubic(p0, p01, p012, m, tol, depth + 1, out);
    out.PushBack(m);
    SubdivideCubic(m, p123, p23, p3, tol, depth + 1, out);
}

// Emits each edge's start + flattened curve interior; the closing point (== out[0]) is intentionally not duplicated.
static void FlattenContour(const Engine::WFontEdge* edges, uint32_t firstEdge, uint32_t edgeCount, double tol, Core::Vector<EcPoint>& out)
{
    for (uint32_t e = 0; e < edgeCount; ++e) {
        const Engine::WFontEdge& edge = edges[firstEdge + e];
        const EcPoint p0{edge.p[0], edge.p[1]};
        out.PushBack(p0);
        if (edge.kind == Engine::WFontEdgeKind::Quadratic) {
            SubdivideQuadratic(p0, {edge.p[2], edge.p[3]}, {edge.p[4], edge.p[5]}, tol, 0, out);
        }
        else if (edge.kind == Engine::WFontEdgeKind::Cubic) {
            SubdivideCubic(p0, {edge.p[2], edge.p[3]}, {edge.p[4], edge.p[5]}, {edge.p[6], edge.p[7]}, tol, 0, out);
        }
    }
}

static double SignedArea(const EcPoint* pts, size_t n)
{
    double s = 0.0;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        s += (pts[j].x * pts[i].y) - (pts[i].x * pts[j].y);
    }
    return s * 0.5;
}

static bool PointInPolygon(EcPoint p, const EcPoint* pts, size_t n)
{
    bool inside = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const bool straddles = (pts[i].y > p.y) != (pts[j].y > p.y);
        if (straddles) {
            const double xCross = (pts[j].x - pts[i].x) * (p.y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x;
            if (p.x < xCross) { inside = !inside; }
        }
    }
    return inside;
}
} // namespace Text3DDetail
} // namespace AssetLoad

namespace mapbox
{
namespace util
{
template<>
struct nth<0, AssetLoad::Text3DDetail::EcPoint>
{
    static double get(const AssetLoad::Text3DDetail::EcPoint& p) { return p.x; }
};
template<>
struct nth<1, AssetLoad::Text3DDetail::EcPoint>
{
    static double get(const AssetLoad::Text3DDetail::EcPoint& p) { return p.y; }
};
} // namespace util
} // namespace mapbox

namespace AssetLoad
{
bool BuildText3DGeometry(const Engine::Font& font, const Engine::Text3DParams& params, Core::TlsfAllocator& scratch,
                         Core::Vector<Engine::FullVertex>& outVertices, Core::Vector<uint32_t>& outIndices)
{
    using namespace Text3DDetail;

    const double tol = std::max(1.0e-5, static_cast<double>(params.flatness));
    const float scale = params.scale;
    const float frontZ = params.depth * 0.5f;
    const float backZ = -params.depth * 0.5f;

    Core::Vector<EcPoint> tmpRing(&scratch, Core::AllocTag::AssetModel);
    Core::Vector<EcPoint> ringPts(&scratch, Core::AllocTag::AssetModel);
    Core::Vector<RingInfo> rings(&scratch, Core::AllocTag::AssetModel);
    Core::Vector<EcPoint> regionPts(&scratch, Core::AllocTag::AssetModel);
    Core::Vector<EcRing> ecRings(&scratch, Core::AllocTag::AssetModel);
    Core::Vector<uint32_t> regionRingIdx(&scratch, Core::AllocTag::AssetModel);
    Core::Vector<EcPoint> edgeNormals(&scratch, Core::AllocTag::AssetModel);

    auto pushCapVertex = [&](const EcPoint& pt, float z, float nz) {
        Engine::FullVertex v{};
        v.position = {static_cast<float>(pt.x) * scale, static_cast<float>(pt.y) * scale, z};
        v.normal = {0.0f, 0.0f, nz};
        v.uv = {static_cast<float>(pt.x) * scale, static_cast<float>(pt.y) * scale};
        v.tangent = {1.0f, 0.0f, 0.0f, nz > 0.0f ? 1.0f : -1.0f};
        v.color = {1.0f, 1.0f, 1.0f, 1.0f};
        outVertices.PushBack(v);
    };

    Engine::Text3DGlyphPlacements placements;
    Engine::LayoutText3D(font, params, placements);

    for (uint32_t p = 0; p < placements.Size(); ++p) {
        const float penX = placements[p].penX;
        const float penY = placements[p].penY;

        const Engine::WGlyphContourRange& gcr = font.glyphContourRanges[placements[p].glyphIndex];
        if (gcr.contourCount > 0) {
            ringPts.Clear();
            rings.Clear();

            // Flatten contours, baking the pen offset in. Doing it here (not at vertex push) keeps the offset a rigid
            // translation ahead of winding/hole classification, which both depend only on relative position.
            for (uint32_t c = 0; c < gcr.contourCount; ++c) {
                const Engine::WContourRange& cr = font.contourRanges[gcr.firstContour + c];
                tmpRing.Clear();
                FlattenContour(font.contourEdges.Data(), cr.firstEdge, cr.edgeCount, tol, tmpRing);
                while (tmpRing.Size() >= 2) {
                    const EcPoint& a = tmpRing[0];
                    const EcPoint& b = tmpRing[tmpRing.Size() - 1];
                    if (std::abs(a.x - b.x) < 1.0e-9 && std::abs(a.y - b.y) < 1.0e-9) { tmpRing.PopBack(); }
                    else { break; }
                }
                if (tmpRing.Size() < 3) { continue; }

                RingInfo info{};
                info.offset = static_cast<uint32_t>(ringPts.Size());
                info.count = static_cast<uint32_t>(tmpRing.Size());
                info.area = SignedArea(tmpRing.Data(), tmpRing.Size());
                for (uint32_t k = 0; k < tmpRing.Size(); ++k) {
                    ringPts.PushBack({tmpRing[k].x + penX, tmpRing[k].y + penY});
                }
                rings.PushBack(info);
            }

            if (!rings.IsEmpty()) {
                // Classify fill vs hole by intrinsic winding, not even-odd nesting: glyphs are often built from
                // overlapping same-winding components (stems/bowls) that nesting miscounts as holes (flipping their
                // walls). The largest contour sets the fill direction; opposite-winding contours are the holes.
                uint32_t largest = 0;
                for (uint32_t a = 1; a < rings.Size(); ++a) {
                    if (std::abs(rings[a].area) > std::abs(rings[largest].area)) { largest = a; }
                }
                const bool fillPositive = rings[largest].area > 0.0;
                for (uint32_t a = 0; a < rings.Size(); ++a) {
                    rings[a].bHole = (rings[a].area > 0.0) != fillPositive;
                }

                // Assign each hole to the tightest enclosing fill so earcut subtracts it from the right region.
                for (uint32_t a = 0; a < rings.Size(); ++a) {
                    if (!rings[a].bHole) { continue; }
                    const EcPoint rep = ringPts[rings[a].offset];
                    double bestArea = 0.0;
                    for (uint32_t b = 0; b < rings.Size(); ++b) {
                        if (rings[b].bHole) { continue; }
                        if (!PointInPolygon(rep, ringPts.Data() + rings[b].offset, rings[b].count)) { continue; }
                        const double absArea = std::abs(rings[b].area);
                        if (rings[a].parent < 0 || absArea < bestArea) {
                            rings[a].parent = static_cast<int32_t>(b);
                            bestArea = absArea;
                        }
                    }
                }

                // Canonicalize winding (fill CCW, hole CW) so the solid is always left of each edge; the wall normals rely on this.
                for (uint32_t a = 0; a < rings.Size(); ++a) {
                    const bool wantPositive = !rings[a].bHole;
                    if ((rings[a].area > 0.0) != wantPositive) {
                        EcPoint* r = ringPts.Data() + rings[a].offset;
                        for (uint32_t lo = 0, hi = rings[a].count - 1; lo < hi; ++lo, --hi) {
                            const EcPoint t = r[lo];
                            r[lo] = r[hi];
                            r[hi] = t;
                        }
                        rings[a].area = -rings[a].area;
                    }
                }

                for (uint32_t oi = 0; oi < rings.Size(); ++oi) {
                    if (rings[oi].bHole) { continue; }

                    regionRingIdx.Clear();
                    regionRingIdx.PushBack(oi);
                    for (uint32_t b = 0; b < rings.Size(); ++b) {
                        if (rings[b].bHole && rings[b].parent == static_cast<int32_t>(oi)) { regionRingIdx.PushBack(b); }
                    }

                    regionPts.Clear();
                    for (uint32_t k = 0; k < regionRingIdx.Size(); ++k) {
                        const RingInfo& ri = rings[regionRingIdx[k]];
                        regionPts.Append(ringPts.Data() + ri.offset, ringPts.Data() + ri.offset + ri.count);
                    }

                    ecRings.Clear();
                    uint32_t off = 0;
                    for (uint32_t k = 0; k < regionRingIdx.Size(); ++k) {
                        const uint32_t cnt = rings[regionRingIdx[k]].count;
                        ecRings.PushBack(EcRing{regionPts.Data() + off, cnt});
                        off += cnt;
                    }

                    const EcPolygon poly{ecRings.Data(), ecRings.Size()};
                    const std::vector<uint32_t> tris = mapbox::earcut<uint32_t>(poly);
                    if (tris.empty()) { continue; }

                    const uint32_t baseFront = static_cast<uint32_t>(outVertices.Size());
                    for (uint32_t k = 0; k < regionPts.Size(); ++k) { pushCapVertex(regionPts[k], frontZ, 1.0f); }
                    const uint32_t baseBack = static_cast<uint32_t>(outVertices.Size());
                    for (uint32_t k = 0; k < regionPts.Size(); ++k) { pushCapVertex(regionPts[k], backZ, -1.0f); }

                    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
                        const uint32_t ia = tris[t], ib = tris[t + 1], ic = tris[t + 2];
                        const double cr = (regionPts[ib].x - regionPts[ia].x) * (regionPts[ic].y - regionPts[ia].y)
                            - (regionPts[ib].y - regionPts[ia].y) * (regionPts[ic].x - regionPts[ia].x);
                        if (cr == 0.0) { continue; }
                        uint32_t f1 = ib, f2 = ic;
                        if (cr < 0.0) { f1 = ic; f2 = ib; } // force front CCW as seen from +Z
                        const uint32_t front[] = {baseFront + ia, baseFront + f1, baseFront + f2};
                        outIndices.Append(front, front + 3);
                        const uint32_t back[] = {baseBack + ia, baseBack + f2, baseBack + f1}; // reversed for -Z facing
                        outIndices.Append(back, back + 3);
                    }
                }

                // Side walls: with the canonical winding each edge's outward face normal is (dy, -dx). Per-vertex normals
                // average the two adjacent edges so curves shade smoothly; a crease angle keeps sharp corners faceted.
                constexpr double CREASE_COS = 0.5; // ~60deg: smaller turns smooth, larger ones (serifs, 'A'/'V' apex) stay sharp
                for (uint32_t a = 0; a < rings.Size(); ++a) {
                    const EcPoint* r = ringPts.Data() + rings[a].offset;
                    const uint32_t n = rings[a].count;

                    edgeNormals.Clear();
                    for (uint32_t e = 0; e < n; ++e) {
                        const double dx = r[(e + 1) % n].x - r[e].x;
                        const double dy = r[(e + 1) % n].y - r[e].y;
                        const double len = std::sqrt(dx * dx + dy * dy);
                        if (len < 1.0e-9) { edgeNormals.PushBack({0.0, 0.0}); }
                        else { edgeNormals.PushBack({dy / len, -dx / len}); }
                    }

                    // Normal at the vertex shared by `edge` and `neighbor`: blend unless smoothing is off or the crease is too sharp.
                    auto vertexNormal = [&](uint32_t edge, uint32_t neighbor) -> EcPoint {
                        const EcPoint self = edgeNormals[edge];
                        if (!params.bSmoothNormals) { return self; }
                        const EcPoint adj = edgeNormals[neighbor];
                        if (adj.x == 0.0 && adj.y == 0.0) { return self; }
                        if (self.x * adj.x + self.y * adj.y <= CREASE_COS) { return self; }
                        EcPoint sum{self.x + adj.x, self.y + adj.y};
                        const double l = std::sqrt(sum.x * sum.x + sum.y * sum.y);
                        if (l < 1.0e-9) { return self; }
                        return {sum.x / l, sum.y / l};
                    };

                    double accum = 0.0;
                    for (uint32_t e = 0; e < n; ++e) {
                        const EcPoint& A = r[e];
                        const EcPoint& B = r[(e + 1) % n];
                        const double dx = B.x - A.x;
                        const double dy = B.y - A.y;
                        const double len = std::sqrt(dx * dx + dy * dy);
                        if (len < 1.0e-9) { continue; }

                        const EcPoint nA = vertexNormal(e, (e + n - 1) % n);
                        const EcPoint nB = vertexNormal(e, (e + 1) % n);
                        const float u0 = static_cast<float>(accum) * scale;
                        const float u1 = static_cast<float>(accum + len) * scale;
                        accum += len;

                        const uint32_t base = static_cast<uint32_t>(outVertices.Size());
                        auto pushWall = [&](const EcPoint& pt, const EcPoint& nrm, float z, float u, float vv) {
                            Engine::FullVertex vert{};
                            vert.position = {static_cast<float>(pt.x) * scale, static_cast<float>(pt.y) * scale, z};
                            vert.normal = {static_cast<float>(nrm.x), static_cast<float>(nrm.y), 0.0f};
                            vert.uv = {u, vv};
                            vert.tangent = {static_cast<float>(-nrm.y), static_cast<float>(nrm.x), 0.0f, 1.0f};
                            vert.color = {1.0f, 1.0f, 1.0f, 1.0f};
                            outVertices.PushBack(vert);
                        };
                        pushWall(A, nA, frontZ, u0, params.depth);
                        pushWall(B, nB, frontZ, u1, params.depth);
                        pushWall(B, nB, backZ, u1, 0.0f);
                        pushWall(A, nA, backZ, u0, 0.0f);

                        const uint32_t wall[] = {base + 0, base + 2, base + 1, base + 0, base + 3, base + 2};
                        outIndices.Append(wall, wall + 6);
                    }
                }
            }
        }
    }

    return !outVertices.IsEmpty() && !outIndices.IsEmpty();
}
} // namespace AssetLoad
