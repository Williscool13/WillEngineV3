//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_TYPES_H
#define WILL_ENGINE_MODEL_TYPES_H

#include <algorithm>
#include <variant>

#include <volk.h>
#include "engine/spline/spline.h"

#include "offsetAllocator.hpp"
#include "core/containers/heap_array.h"
#include "engine/resources/material/material.h"
#include "engine/asset_manager_types.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "core/math/constants.h"
#include "core/types/math.h"
#include "engine/reflection/reflection.h"

namespace Render
{
struct ResourceManager;
}

namespace Engine
{
enum class MaterialType
{
    SOLID = 0,
    BLEND = 1,
    CUTOUT = 2,
};

struct PrimitiveProperty
{
    uint32_t index;
    int32_t materialIndex;
    uint32_t triangleCount{0};
    uint32_t meshletCount{0}; // LOD0

    Vec3 boundingBoxMin{};
    Vec3 boundingBoxMax{};
    Vec4 boundingSphere{};

    // Raytracing
    uint64_t blasHandle{0};
    uint64_t blasDeviceAddress{0};
    OffsetAllocator::Allocation blasAllocation{};
};

inline constexpr uint32_t MAX_PRIMITIVES_PER_MESH = 128;

struct MeshInformation
{
    Core::InlineString<> name;
    // todo perhaps increase this limit.
    Core::InlineVector<PrimitiveProperty, MAX_PRIMITIVES_PER_MESH> primitiveProperties;
};

struct Node
{
    Core::InlineString<> name{};
    uint32_t parent{~0u};
    uint32_t meshIndex{~0u};
    uint32_t depth{};

    uint32_t inverseBindIndex{~0u};

    Vec3 localTranslation{0.0f};
    Quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 localScale{1.0f};
};

struct StaticModelData
{
    Core::HeapArray<MeshInformation> meshes{};
    Core::HeapArray<Node> nodes{};
    Core::HeapArray<Material> materials{};

    OffsetAllocator::Allocation vertexPositionAllocation{};
    OffsetAllocator::Allocation indexAllocation{};
    OffsetAllocator::Allocation meshletVertexAllocation{};
    OffsetAllocator::Allocation meshletTriangleAllocation{};
    OffsetAllocator::Allocation meshletAllocation{};
    OffsetAllocator::Allocation primitiveAllocation{};

    StaticModelData() = default;

    StaticModelData(const StaticModelData&) = delete;

    StaticModelData& operator=(const StaticModelData&) = delete;

    StaticModelData(StaticModelData&&) noexcept = default;

    StaticModelData& operator=(StaticModelData&&) noexcept = default;

    void Reset(Render::ResourceManager* resourceManager);
};

/**
 * Not meant for model use. Only for vertex processing.
 */
struct FullVertex
{
    Vec3 position;
    Vec2 uv;
    Vec3 normal;
    Vec4 tangent;
    Vec4 color;
};

/**
 * Compacted vertex meant for storage. Position is loaded into a separate buffer form the rest of the vertex attributes.
 */
struct Vertex
{
    uint32_t pos0; // unorm16: bits[15:0]=X, bits[31:16]=Y
    uint32_t pos1; // unorm16: bits[15:0]=Z, bits[31:16]=unused
    uint32_t normalOct; // snorm16: bits[15:0]=X, bits[31:16]=Y
    uint32_t tangentOct; // snorm15: bits[14:0]=X, bits[29:15]=Y; bit[30]=sign(0=neg,1=pos)
    uint32_t texcoord; // float16: bits[15:0]=U, bits[31:16]=V
    uint32_t color; // unorm8: bits[7:0]=R, bits[15:8]=G, bits[23:16]=B, bits[31:24]=A
};

struct StaircaseParams
{
    int32_t stepCount{10};
    float width{1.0f};
    float totalDepth{3.0f};
    float totalHeight{2.0f};
    bool bSpecifyStepHeight{false};
    uint8_t _pad0[3]{};
    float stepHeight{0.2f};
    bool bIsClosed{true};
    uint8_t _pad1[3]{};
    bool bSpecifyStepDepth{false};
    uint8_t _pad2[3]{};
    float stepDepth{0.2f};
    float slabThickness{0.0f};
    float sideWallHeight{0.0f};
    float sideWallThickness{0.2f};
    float sideWallStart{0.0f};
    bool bSideWallNegX{true};
    bool bSideWallPosX{true};
    uint8_t _pad3[2]{};
    float stepChamfer{0.0f};
    float sideWallChamfer{0.0f};
    /** Sideways (+X) slide per step, so the flight runs on a diagonal; stepShift = step depth follows a 45 degree edge. */
    float stepShift{0.0f};
    /** Width at the top of the run, blending linearly from width; 0 keeps width. */
    float endWidth{0.0f};

    WILL_REFLECT(StaircaseParams,WILL_FIELD(stepCount), WILL_FIELD(width), WILL_FIELD(totalDepth), WILL_FIELD(totalHeight), WILL_FIELD(bSpecifyStepHeight),
                 WILL_FIELD(stepHeight), WILL_FIELD(bIsClosed), WILL_FIELD(bSpecifyStepDepth), WILL_FIELD(stepDepth), WILL_FIELD(slabThickness),
                 WILL_FIELD(sideWallHeight), WILL_FIELD(sideWallThickness), WILL_FIELD(sideWallStart), WILL_FIELD(bSideWallNegX), WILL_FIELD(bSideWallPosX),
                 WILL_FIELD(stepChamfer), WILL_FIELD(sideWallChamfer), WILL_FIELD(stepShift), WILL_FIELD(endWidth))
};

/** totalDepth, or stepCount * stepDepth when the step depth is specified. */
inline float StaircaseTotalDepth(const StaircaseParams& p)
{
    return p.bSpecifyStepDepth ? p.stepDepth * static_cast<float>(p.stepCount) : p.totalDepth;
}

inline float StaircaseStepHeight(const StaircaseParams& p)
{
    return (p.bSpecifyStepHeight && p.stepHeight > 0.0f) ? p.stepHeight : p.totalHeight / static_cast<float>(glm::max(p.stepCount, 1));
}

inline bool StaircaseIsSkewed(const StaircaseParams& p)
{
    return p.stepShift != 0.0f || (p.endWidth > 0.0f && p.endWidth != p.width);
}

/** Maps x of a straight flight (0..width across, side walls outside it) at run distance z onto the skewed, tapered flight; walls keep their thickness. */
inline float StaircaseSkewX(const StaircaseParams& p, float x, float z)
{
    const float depth = glm::max(StaircaseTotalDepth(p), DIVISION_EPSILON);
    const float stepDepth = depth / static_cast<float>(glm::max(p.stepCount, 1));
    const float w = glm::mix(p.width, p.endWidth > 0.0f ? p.endWidth : p.width, glm::clamp(z / depth, 0.0f, 1.0f));
    const float shift = p.stepShift * z / stepDepth;
    if (x <= 0.0f) { return shift + x; }
    if (x >= p.width) { return shift + w + (x - p.width); }
    return shift + x * w / glm::max(p.width, DIVISION_EPSILON);
}

inline float StaircaseStepTop(const StaircaseParams& p, int32_t step)
{
    return step < p.stepCount - 1 ? static_cast<float>(step + 1) * StaircaseStepHeight(p) : p.totalHeight;
}

inline bool StaircaseIsSlab(const StaircaseParams& p)
{
    return p.slabThickness > 0.0f && p.slabThickness < p.totalHeight;
}

inline bool StaircaseHasSideWalls(const StaircaseParams& p)
{
    return p.sideWallHeight > 0.0f && p.sideWallThickness > 0.0f && (p.bSideWallNegX || p.bSideWallPosX) && p.sideWallStart < StaircaseTotalDepth(p);
}

/** Height of the line through the riser feet at depth z. */
inline float StaircaseLineHeight(const StaircaseParams& p, float z)
{
    const float stepDepth = StaircaseTotalDepth(p) / static_cast<float>(glm::max(p.stepCount, 1));
    return StaircaseStepHeight(p) * z / glm::max(stepDepth, DIVISION_EPSILON);
}

/** Underside height at depth z: the floor, or for a slab slabThickness below the riser feet, stopping at the floor. */
inline float StaircaseUndersideHeight(const StaircaseParams& p, float z)
{
    if (!StaircaseIsSlab(p)) { return 0.0f; }
    return glm::clamp(StaircaseLineHeight(p, z) - p.slabThickness, 0.0f, p.totalHeight - p.slabThickness);
}

/**
 * Depths where the underside changes slope inside [z0, z1], ends included, ascending.
 * @return count written to out
 */
inline int32_t StaircaseUndersideBreaks(const StaircaseParams& p, float z0, float z1, float (&out)[4])
{
    int32_t count = 0;
    out[count++] = z0;
    if (StaircaseIsSlab(p)) {
        const float run = StaircaseTotalDepth(p) / static_cast<float>(glm::max(p.stepCount, 1)) / glm::max(StaircaseStepHeight(p), DIVISION_EPSILON);
        const float kinks[2] = {p.slabThickness * run, p.totalHeight * run};
        for (const float kink : kinks) {
            if (kink > out[count - 1] + GEOMETRY_EPSILON && kink < z1 - GEOMETRY_EPSILON) { out[count++] = kink; }
        }
    }
    out[count++] = z1;
    return count;
}

/** 1 is a flat bevel; more segments round the chamfered edge into an arc tangent to both faces. */
inline constexpr int32_t MAX_CHAMFER_SEGMENTS = 8;

struct BoxParams
{
    float sizeX{1.0f}, sizeY{1.0f}, sizeZ{1.0f};
    float chamferX[4]{};
    float chamferY[4]{};
    float chamferZ[4]{};
    int32_t chamferSegments{1};

    WILL_REFLECT(BoxParams, WILL_FIELD(sizeX), WILL_FIELD(sizeY), WILL_FIELD(sizeZ), WILL_FIELD(chamferX), WILL_FIELD(chamferY), WILL_FIELD(chamferZ), WILL_FIELD(chamferSegments))
};

struct CylinderParams
{
    float radius{0.5f};
    float height{2.0f};
    int32_t slices{16};
    bool bCapped{true};
    uint8_t _pad0[3]{};

    WILL_REFLECT(CylinderParams, WILL_FIELD(radius), WILL_FIELD(height), WILL_FIELD(slices), WILL_FIELD(bCapped))
};

struct CapsuleParams
{
    float radius{0.5f};
    float height{2.0f};
    int32_t slices{16};
    int32_t rings{8};

    WILL_REFLECT(CapsuleParams, WILL_FIELD(radius), WILL_FIELD(height), WILL_FIELD(slices), WILL_FIELD(rings))
};

struct TorusParams
{
    float ringRadius{1.0f};
    float tubeRadius{0.25f};
    int32_t slices{16};
    int32_t stacks{16};

    WILL_REFLECT(TorusParams, WILL_FIELD(ringRadius), WILL_FIELD(tubeRadius), WILL_FIELD(slices), WILL_FIELD(stacks))
};

struct ArchParams
{
    float width{2.0f};
    float height{2.5f};
    float depth{0.5f};
    float thickness{0.3f};
    int32_t sides{8};
    bool bFillCorners{false};
    uint8_t _pad0[3]{};

    WILL_REFLECT(ArchParams, WILL_FIELD(width), WILL_FIELD(height), WILL_FIELD(depth), WILL_FIELD(thickness), WILL_FIELD(sides), WILL_FIELD(bFillCorners))
};

struct WedgeParams
{
    float sizeX{1.0f};
    float sizeY{1.0f};
    float sizeZ{1.0f};
    float baseHeight{0.0f};
    float chamferX[4]{};
    float chamferY[4]{};
    float chamferZ[4]{};
    float slabThickness{0.0f};
    int32_t chamferSegments{1};

    WILL_REFLECT(WedgeParams, WILL_FIELD(sizeX), WILL_FIELD(sizeY), WILL_FIELD(sizeZ), WILL_FIELD(baseHeight), WILL_FIELD(chamferX), WILL_FIELD(chamferY), WILL_FIELD(chamferZ),
                 WILL_FIELD(slabThickness), WILL_FIELD(chamferSegments))
};

inline bool WedgeIsSlab(const WedgeParams& p)
{
    return p.slabThickness > 0.0f && p.slabThickness < glm::max(p.baseHeight, 0.0f) + p.sizeY;
}

struct ConeParams
{
    float radius{0.5f};
    float height{2.0f};
    int32_t slices{16};
    /** Closes the base. */
    bool bCapped{true};
    /** Closes the cut tip; only used when topRadius is above 0. */
    bool bTopCapped{true};
    uint8_t _pad0[2]{};
    /** 0 is a pointed apex; above 0 the tip is cut flat (a frustum). */
    float topRadius{0.0f};

    WILL_REFLECT(ConeParams, WILL_FIELD(radius), WILL_FIELD(height), WILL_FIELD(slices), WILL_FIELD(bCapped), WILL_FIELD(bTopCapped), WILL_FIELD(topRadius))
};

/**
 * archHeight=0 → flat-topped rectangle. archHeight=width/2 → full semicircle. archHeight>width/2 → Gothic pointed arch.
 */
struct DoorParams
{
    float width{1.0f};
    float height{2.0f};
    float depth{0.05f};
    float archHeight{0.5f};
    float gap{0.0f};
    int32_t sides{8};
    bool bHalf{false};
    bool bFlip{false};
    uint8_t _pad0[2]{};

    WILL_REFLECT(DoorParams, WILL_FIELD(width), WILL_FIELD(height), WILL_FIELD(depth), WILL_FIELD(archHeight), WILL_FIELD(gap), WILL_FIELD(sides), WILL_FIELD(bHalf),
                 WILL_FIELD(bFlip))
};

struct PlaneParams
{
    float sizeX{2.0f};
    float sizeZ{2.0f};
    int32_t tilesX{1};
    int32_t tilesZ{1};

    WILL_REFLECT(PlaneParams, WILL_FIELD(sizeX), WILL_FIELD(sizeZ), WILL_FIELD(tilesX), WILL_FIELD(tilesZ))
};

struct SphereParams
{
    float radius{0.5f};
    int32_t slices{16};
    int32_t stacks{8};

    WILL_REFLECT(SphereParams, WILL_FIELD(radius), WILL_FIELD(slices), WILL_FIELD(stacks))
};

struct SubdividedSphereParams
{
    float radius{0.5f};
    int32_t subdivisions{3};

    WILL_REFLECT(SubdividedSphereParams, WILL_FIELD(radius), WILL_FIELD(subdivisions))

    static void Sanitize(SubdividedSphereParams& p) { p.subdivisions = glm::clamp(p.subdivisions, 0, 4); }
};

struct HemisphereParams
{
    float radius{0.5f};
    int32_t slices{16};
    int32_t stacks{8};
    /** 0 is a solid dome with a base cap; above 0 it is a hollow shell (outer + inner dome + base rim), concave triangle-mesh collider. */
    float innerRadius{0.0f};
    /** Dome height as a fraction of radius; below 1 flattens it into an elliptical cap. */
    float heightScale{1.0f};

    WILL_REFLECT(HemisphereParams, WILL_FIELD(radius), WILL_FIELD(slices), WILL_FIELD(stacks), WILL_FIELD(innerRadius), WILL_FIELD(heightScale))

    [[nodiscard]] bool IsHollow() const { return innerRadius > POSITION_EPSILON && innerRadius < radius; }
};

struct PipeParams
{
    float outerRadius{0.5f};
    float innerRadius{0.3f};
    float height{2.0f};
    int32_t slices{16};
    /** How far the inner edge sits below the outer edge; the top and bottom slope straight between them (concave triangle-mesh collider, static-only). */
    float innerDrop{0.0f};
    /** Flat band in from the outer edge before the slope starts. */
    float lipWidth{0.0f};
    /** Degrees of arc from +X toward +Z; below 360 the open ends are capped. Slices stay per full circle, so partial arcs share a full pipe's facets. */
    float sweepDegrees{360.0f};
    /** One normal per facet instead of smooth radial normals, so low slice counts shade as flat panels. */
    bool bFlatShaded{false};
    /** Close the bore at that end with a plug capThickness deep (0 = the full height); both caps meeting make the pipe solid. */
    bool bCapTop{false};
    bool bCapBottom{false};
    uint8_t _pad0[1]{};
    float capThickness{0.0f};

    WILL_REFLECT(PipeParams, WILL_FIELD(outerRadius), WILL_FIELD(innerRadius), WILL_FIELD(height), WILL_FIELD(slices), WILL_FIELD(innerDrop), WILL_FIELD(lipWidth),
                 WILL_FIELD(sweepDegrees), WILL_FIELD(bFlatShaded), WILL_FIELD(bCapTop), WILL_FIELD(bCapBottom), WILL_FIELD(capThickness))

    [[nodiscard]] bool IsSloped() const { return glm::abs(innerDrop) > POSITION_EPSILON; }
    [[nodiscard]] float ClampedSweepDegrees() const { return glm::clamp(sweepDegrees, 1.0f, 360.0f); }
    [[nodiscard]] bool IsPartial() const { return ClampedSweepDegrees() < 360.0f - 1e-3f; }
    /** Plug depth per capped end, clamped so two caps never overlap; the bore is solid when they meet. */
    [[nodiscard]] float CapDepth() const
    {
        const float full = glm::max(height, 0.001f);
        return glm::clamp(capThickness > 0.0f ? capThickness : full, 0.001f, full * ((bCapTop && bCapBottom) ? 0.5f : 1.0f));
    }
    /** Facets in the swept arc at the full-circle slice density, at least one. */
    [[nodiscard]] int32_t SweepSegments() const
    {
        return glm::max(1, static_cast<int32_t>(glm::ceil(static_cast<float>(glm::max(3, slices)) * ClampedSweepDegrees() / 360.0f - 1e-3f)));
    }
};

struct TetrahedronParams
{
    float radius{0.5f};

    WILL_REFLECT(TetrahedronParams, WILL_FIELD(radius))
};

struct OctahedronParams
{
    float radius{0.5f};

    WILL_REFLECT(OctahedronParams, WILL_FIELD(radius))
};

struct IcosahedronParams
{
    float radius{0.5f};

    WILL_REFLECT(IcosahedronParams, WILL_FIELD(radius))
};

struct DodecahedronParams
{
    float radius{0.5f};

    WILL_REFLECT(DodecahedronParams, WILL_FIELD(radius))
};

struct KleinBottleParams
{
    float scale{1.0f};
    int32_t slices{8};
    int32_t stacks{8};

    WILL_REFLECT(KleinBottleParams, WILL_FIELD(scale), WILL_FIELD(slices), WILL_FIELD(stacks))
};

struct TrefoilKnotParams
{
    float scale{1.0f};
    float tubeRadius{1.0f};
    int32_t slices{16};
    int32_t stacks{128};

    WILL_REFLECT(TrefoilKnotParams, WILL_FIELD(scale), WILL_FIELD(tubeRadius), WILL_FIELD(slices), WILL_FIELD(stacks))
};

enum class SplineProfileType : uint8_t
{
    Tube = 0,
    Rectangle = 1,
    RoundedRect = 2,
    IBeam = 3,
    UChannel = 4,
    LAngle = 5,
    RailHead = 6,
    Handrail = 7,
    /** Stands on the path (y from 0 to height) with the corners picked by wallChamfer and wallChamferEdge cut by cornerRadius; cornerSegments above 1 rounds the cut. */
    Wall = 8,
};

enum class SplineWallChamfer : uint8_t
{
    Both = 0,
    Left = 1,
    Right = 2,
};

enum class SplineWallChamferEdge : uint8_t
{
    Top = 0,
    Bottom = 1,
    Both = 2,
};

struct SplineProfile
{
    SplineProfileType type{SplineProfileType::Tube};
    float width{0.4f};
    float height{0.4f};
    float cornerRadius{0.08f};
    int32_t cornerSegments{3};
    float thickness{0.05f};
    SplineWallChamfer wallChamfer{SplineWallChamfer::Both};
    SplineWallChamferEdge wallChamferEdge{SplineWallChamferEdge::Top};

    WILL_REFLECT(SplineProfile, WILL_FIELD(type), WILL_FIELD(width), WILL_FIELD(height), WILL_FIELD(cornerRadius), WILL_FIELD(cornerSegments), WILL_FIELD(thickness),
                 WILL_FIELD(wallChamfer), WILL_FIELD(wallChamferEdge))
};

enum class SplinePostShape : uint8_t
{
    Box = 0,
    Cylinder = 1,
    Capsule = 2,
};

struct SplineRailing
{
    bool bEnabled{false};
    Core::InlineVector<Vec2, 8> lanes{};
    bool bPosts{true};
    int32_t postInterval{4};
    float postBottom{0.0f};
    float postTop{1.0f};
    /** Box: width x depth. Cylinder/Capsule: x is the diameter. */
    Vec2 postSize{0.05f, 0.05f};
    float postLateral{0.0f};
    float lateralOffset{0.0f};
    SplinePostShape postShape{SplinePostShape::Box};
    int32_t postSides{12};
    /** Posts per postInterval rings; spacing is postInterval / postsPerInterval rings, so above 1 places posts between rings. */
    int32_t postsPerInterval{1};
    /** Per-lane (width, height) multiplier on the profile, parallel to lanes; a missing entry is 1. */
    Core::InlineVector<Vec2, 8> laneScales{};

    WILL_REFLECT(SplineRailing, WILL_FIELD(bEnabled), WILL_FIELD(lanes), WILL_FIELD(bPosts), WILL_FIELD(postInterval), WILL_FIELD(postBottom), WILL_FIELD(postTop),
                 WILL_FIELD(postSize), WILL_FIELD(postLateral), WILL_FIELD(lateralOffset), WILL_FIELD(postShape), WILL_FIELD(postSides), WILL_FIELD(postsPerInterval),
                 WILL_FIELD(laneScales))

    /** Ring-index step between posts. */
    [[nodiscard]] float PostSpacing() const { return static_cast<float>(std::max(1, postInterval)) / static_cast<float>(std::clamp(postsPerInterval, 1, 64)); }

    [[nodiscard]] Vec2 LaneScale(size_t lane) const
    {
        const Vec2 s = lane < laneScales.Size() ? laneScales[lane] : Vec2{1.0f, 1.0f};
        return glm::max(s, Vec2{0.01f, 0.01f});
    }

    /** Smallest scale component over all lanes, so posts clamp to the narrowest rail they meet. */
    [[nodiscard]] float MinLaneScale() const
    {
        float m = 1.0f;
        for (size_t i = 0; i < lanes.Size(); i++) {
            const Vec2 s = LaneScale(i);
            m = i == 0 ? std::min(s.x, s.y) : std::min(m, std::min(s.x, s.y));
        }
        return m;
    }
};

struct SplineParams
{
    Spline spline;
    float radius{0.5f};
    float rollAngle{0.0f};
    int32_t sides{8};
    int32_t segmentsPerSpan{8};
    bool bCaps{true};
    bool bCrossPlanks{false};
    int32_t crossPlankInterval{4};
    float crossPlankHeight{0.0f};
    float crossPlankThickness{0.1f};
    float crossPlankLength{0.3f};
    SplineProfile profile{};
    SplineRailing railing{};
    /** Tube wall grown outward from radius (the bore stays radius); 0 = a single surface. Plain tubes only, no railing. */
    float tubeThickness{0.0f};
    /** Tube sides shade flat instead of round. */
    bool bFacetNormals{false};

    WILL_REFLECT(SplineParams, WILL_FIELD(spline), WILL_FIELD(radius), WILL_FIELD(rollAngle), WILL_FIELD(sides), WILL_FIELD(segmentsPerSpan), WILL_FIELD(bCaps),
                 WILL_FIELD(bCrossPlanks), WILL_FIELD(crossPlankInterval), WILL_FIELD(crossPlankHeight), WILL_FIELD(crossPlankThickness), WILL_FIELD(crossPlankLength),
                 WILL_FIELD(profile), WILL_FIELD(railing), WILL_FIELD(tubeThickness), WILL_FIELD(bFacetNormals))

    [[nodiscard]] float TubeWall() const { return profile.type == SplineProfileType::Tube && !railing.bEnabled ? glm::max(tubeThickness, 0.0f) : 0.0f; }
};

struct BowlParams
{
    float radius{2.0f};
    float height{2.0f};
    float curveRadius{2.0f};
    float flatRadius{0.0f};
    float lipHeight{0.02f};
    int32_t slices{16};
    int32_t segments{8};

    WILL_REFLECT(BowlParams, WILL_FIELD(radius), WILL_FIELD(height), WILL_FIELD(curveRadius), WILL_FIELD(flatRadius), WILL_FIELD(lipHeight), WILL_FIELD(slices),
                 WILL_FIELD(segments))
};

struct CurvedRampParams
{
    float width{2.0f};
    float height{2.0f};
    float radius{2.0f};
    int32_t segments{8};
    bool bHalfPipe{false};
    uint8_t _pad0[3]{};
    float flatLength{1.0f};
    float lipHeight{0.02f};

    WILL_REFLECT(CurvedRampParams, WILL_FIELD(width), WILL_FIELD(height), WILL_FIELD(radius), WILL_FIELD(segments), WILL_FIELD(bHalfPipe), WILL_FIELD(flatLength),
                 WILL_FIELD(lipHeight))
};

/**
 * centerColumnRadius insets the tread inner edge (and sizes the column). bShowCenterColumn only toggles the column mesh; the treads inset regardless.
 * bSpecifyStepHeight=false derives riser from totalHeight/stepCount (fix the rise); true uses stepHeight literally and totalHeight is informational.
 * bSpecifyDegreesPerStep=false derives per-step rotation from totalSweep/stepCount (fix the turn); true uses degreesPerStep literally and totalSweep is informational. Rotation never drives stepCount (height owns it).
 */
struct SpiralStaircaseParams
{
    int32_t stepCount{12};
    float stepHeight{0.2f};
    float totalHeight{2.4f};
    bool bSpecifyStepHeight{false};
    uint8_t _pad0[3]{};
    float outerRadius{1.5f};
    float centerColumnRadius{0.25f};
    float treadThickness{0.08f};
    float degreesPerStep{30.0f};
    float totalSweep{360.0f};
    bool bSpecifyDegreesPerStep{false};
    uint8_t _pad1[3]{};
    int32_t arcSegments{6};
    bool bShowCenterColumn{true};
    bool bRamp{false};
    uint8_t _pad2[2]{};

    WILL_REFLECT(SpiralStaircaseParams, WILL_FIELD(stepCount), WILL_FIELD(stepHeight), WILL_FIELD(totalHeight), WILL_FIELD(bSpecifyStepHeight), WILL_FIELD(outerRadius),
                 WILL_FIELD(centerColumnRadius), WILL_FIELD(treadThickness), WILL_FIELD(degreesPerStep), WILL_FIELD(totalSweep), WILL_FIELD(bSpecifyDegreesPerStep),
                 WILL_FIELD(arcSegments), WILL_FIELD(bShowCenterColumn), WILL_FIELD(bRamp))
};

/**
 * Flat annular disc in the y=0 plane (a disc with a hole). innerRadius=0 collapses to a solid disc.
 * bDoubleSided emits a back face (-Y) so it is visible from below; single-sided is +Y only.
 */
struct RingParams
{
    float outerRadius{0.5f};
    float innerRadius{0.25f};
    int32_t slices{32};
    bool bDoubleSided{true};
    uint8_t _pad0[3]{};

    WILL_REFLECT(RingParams, WILL_FIELD(outerRadius), WILL_FIELD(innerRadius), WILL_FIELD(slices), WILL_FIELD(bDoubleSided))
};

struct WallOpening
{
    float x{0.0f};
    float y{0.0f};
    float w{0.0f};
    float h{0.0f};

    WILL_REFLECT(WallOpening, WILL_FIELD(x), WILL_FIELD(y), WILL_FIELD(w), WILL_FIELD(h))
};

/**
 * Slab with up to MAX_OPENINGS rectangular holes through its thickness (Z). Corner pivot like BoxParams; openings are (x, y, w, h) in the face plane.
 */
struct WallParams
{
    static constexpr int32_t MAX_OPENINGS = 8;

    float sizeX{4.0f};
    float sizeY{3.0f};
    float sizeZ{0.2f};
    int32_t openingCount{0};
    WallOpening openings[MAX_OPENINGS]{};

    WILL_REFLECT(WallParams, WILL_FIELD(sizeX), WILL_FIELD(sizeY), WILL_FIELD(sizeZ), WILL_FIELD(openingCount), WILL_FIELD(openings))
};

/**
 * Straight 4-chord truss inside the corner-pivot envelope (0,0,0)..(size), long axis Y. Chords run the
 * full height at the XZ footprint corners; bayCount bays stack along Y with a horizontal ring of braces
 * at every bay boundary. pattern 0 = X-brace (both diagonals per side face per bay), 1 = single diagonal
 * alternating direction per bay. Members are square-section boxes meeting at chord centerlines.
 */
struct LatticeParams
{
    float sizeX{0.5f};
    float sizeY{3.0f};
    float sizeZ{0.5f};
    float chordSize{0.06f};
    float braceSize{0.04f};
    int32_t bayCount{4};
    int32_t pattern{0};

    WILL_REFLECT(LatticeParams, WILL_FIELD(sizeX), WILL_FIELD(sizeY), WILL_FIELD(sizeZ), WILL_FIELD(chordSize), WILL_FIELD(braceSize), WILL_FIELD(bayCount), WILL_FIELD(pattern))
};

/**
 * Corrugated sheet: flat back at z=0, web sizeZ thick, ribCount trapezoidal ribs protruding to
 * z = sizeZ + ribDepth, evenly pitched across X and running the full Y. Each rib is a plateau of
 * ribWidth with flanks flankAngle from vertical (0 = square fins), steepened when the pitch cannot fit them.
 * When alternateEvery >= 2, every alternateEvery-th rib uses alternateDepth instead. Corner pivot like BoxParams.
 */
struct CorrugatedPanelParams
{
    float sizeX{2.4f};
    float sizeY{2.4f};
    float sizeZ{0.05f};
    float ribDepth{0.05f};
    float ribWidth{0.2f};
    int32_t ribCount{6};
    float flankAngle{45.0f};
    int32_t alternateEvery{0};
    float alternateDepth{0.0f};

    WILL_REFLECT(CorrugatedPanelParams, WILL_FIELD(sizeX), WILL_FIELD(sizeY), WILL_FIELD(sizeZ), WILL_FIELD(ribDepth), WILL_FIELD(ribWidth), WILL_FIELD(ribCount),
                 WILL_FIELD(flankAngle), WILL_FIELD(alternateEvery), WILL_FIELD(alternateDepth))
};

struct CorrugatedRib
{
    float center;
    float width;
    float depth;
    float flank;
};

/** Rib i's profile, shared by the mesh and collider generators. */
inline CorrugatedRib CorrugatedPanelRib(const CorrugatedPanelParams& p, int32_t i)
{
    const int32_t ribs = glm::max(1, p.ribCount);
    const float pitch = p.sizeX / static_cast<float>(ribs);
    const float w = glm::clamp(p.ribWidth, 0.0f, glm::max(0.0f, pitch - 2e-3f));
    const bool bAlternate = p.alternateEvery >= 2 && (i % p.alternateEvery) == p.alternateEvery - 1;
    const float depth = glm::max(bAlternate ? p.alternateDepth : p.ribDepth, 0.0f);
    const float slope = glm::tan(glm::radians(glm::clamp(p.flankAngle, 0.0f, 89.0f)));
    const float flank = glm::min(depth * slope, (pitch - w) * 0.5f);
    return {(static_cast<float>(i) + 0.5f) * pitch, w, depth, flank};
}

enum TerraceSide : int32_t
{
    TERRACE_SIDE_NEG_X = 1 << 0,
    TERRACE_SIDE_POS_X = 1 << 1,
    TERRACE_SIDE_NEG_Z = 1 << 2,
    TERRACE_SIDE_POS_Z = 1 << 3,
};

enum class TerraceDirection : uint8_t
{
    Up,
    Down,
};

enum class TerraceProfile : uint8_t
{
    Steps,
    Ramp,
};

/**
 * Stepped block over a corner-pivot sizeX x sizeZ footprint, inset by stepRun on the enabled sides. Up climbs to a central
 * landing; Down descends to a floor at baseHeight, and a baseHeight of 0 leaves a hole through. bFloor off removes the innermost level,
 * leaving an open-ended tube. lipWidth extends the outermost level past the footprint on the stepped sides.
 */
struct TerraceParams
{
    float sizeX{4.0f};
    float sizeZ{4.0f};
    int32_t stepCount{3};
    float stepRise{0.2f};
    float stepRun{0.3f};
    float baseHeight{0.2f};
    int32_t sides{TERRACE_SIDE_NEG_X | TERRACE_SIDE_POS_X | TERRACE_SIDE_NEG_Z | TERRACE_SIDE_POS_Z};
    TerraceDirection direction{TerraceDirection::Down};
    bool bFloor{true};
    TerraceProfile profile{TerraceProfile::Steps};
    uint8_t _pad0[1]{};
    float lipWidth{0.0f};
    float stepChamfer{0.0f};

    static constexpr int32_t MAX_STEPS = 64;

    WILL_REFLECT(TerraceParams, WILL_FIELD(sizeX), WILL_FIELD(sizeZ), WILL_FIELD(stepCount), WILL_FIELD(stepRise), WILL_FIELD(stepRun), WILL_FIELD(baseHeight),
                 WILL_FIELD(sides), WILL_FIELD(direction), WILL_FIELD(bFloor), WILL_FIELD(profile), WILL_FIELD(lipWidth), WILL_FIELD(stepChamfer))

    static void Sanitize(TerraceParams& p)
    {
        p.sizeX = glm::max(p.sizeX, 0.001f);
        p.sizeZ = glm::max(p.sizeZ, 0.001f);
        p.stepCount = glm::clamp(p.stepCount, 1, MAX_STEPS);
        p.stepRise = glm::max(p.stepRise, 0.0f);
        p.stepRun = glm::max(p.stepRun, 0.0f);
        p.baseHeight = glm::max(p.baseHeight, 0.0f);
        p.lipWidth = glm::max(p.lipWidth, 0.0f);
        p.stepChamfer = glm::max(p.stepChamfer, 0.0f);
        p.sides &= TERRACE_SIDE_NEG_X | TERRACE_SIDE_POS_X | TERRACE_SIDE_NEG_Z | TERRACE_SIDE_POS_Z;
    }
};

struct TerraceRect
{
    float x0, z0, x1, z1;
};

/** Levels run outermost (0) to innermost; Down adds a rim level. */
inline int32_t TerraceLevelCount(const TerraceParams& p)
{
    return p.direction == TerraceDirection::Down ? p.stepCount + 1 : p.stepCount;
}

/** stepRun clamped so the innermost level's rect never inverts. */
inline float TerraceRun(const TerraceParams& p)
{
    const int32_t insets = TerraceLevelCount(p) - 1;
    float run = p.stepRun;
    if (insets <= 0) { return run; }
    const int32_t sidesX = ((p.sides & TERRACE_SIDE_NEG_X) ? 1 : 0) + ((p.sides & TERRACE_SIDE_POS_X) ? 1 : 0);
    const int32_t sidesZ = ((p.sides & TERRACE_SIDE_NEG_Z) ? 1 : 0) + ((p.sides & TERRACE_SIDE_POS_Z) ? 1 : 0);
    if (sidesX > 0) { run = glm::min(run, p.sizeX / static_cast<float>(sidesX * insets)); }
    if (sidesZ > 0) { run = glm::min(run, p.sizeZ / static_cast<float>(sidesZ * insets)); }
    return glm::max(run, 0.0f);
}

inline TerraceRect TerraceLevelRect(const TerraceParams& p, float run, int32_t level)
{
    const float d = level == 0 ? -p.lipWidth : run * static_cast<float>(level);
    return {
        (p.sides & TERRACE_SIDE_NEG_X) ? d : 0.0f,
        (p.sides & TERRACE_SIDE_NEG_Z) ? d : 0.0f,
        p.sizeX - ((p.sides & TERRACE_SIDE_POS_X) ? d : 0.0f),
        p.sizeZ - ((p.sides & TERRACE_SIDE_POS_Z) ? d : 0.0f),
    };
}

/** Top height of a level; 0 means empty. */
inline float TerraceLevelHeight(const TerraceParams& p, int32_t level)
{
    if (p.direction == TerraceDirection::Down) {
        return p.baseHeight + p.stepRise * static_cast<float>(p.stepCount - level);
    }
    return p.baseHeight + p.stepRise * static_cast<float>(level + 1);
}

inline constexpr float TERRACE_NO_INSET = 1e30f;

/** Inset distance of the innermost level; with bFloor off everything deeper is a hole. */
inline float TerraceHoleInset(const TerraceParams& p, float run)
{
    return run * static_cast<float>(TerraceLevelCount(p) - 1);
}

/** Distance from x to the nearest stepped X side, or TERRACE_NO_INSET if neither is stepped. */
inline float TerraceInsetX(const TerraceParams& p, float x)
{
    float d = TERRACE_NO_INSET;
    if (p.sides & TERRACE_SIDE_NEG_X) { d = glm::min(d, x); }
    if (p.sides & TERRACE_SIDE_POS_X) { d = glm::min(d, p.sizeX - x); }
    return d;
}

inline float TerraceInsetZ(const TerraceParams& p, float z)
{
    float d = TERRACE_NO_INSET;
    if (p.sides & TERRACE_SIDE_NEG_Z) { d = glm::min(d, z); }
    if (p.sides & TERRACE_SIDE_POS_Z) { d = glm::min(d, p.sizeZ - z); }
    return d;
}

/** Ramp height at inset distance d; the slope runs through the step nosings. */
inline float TerraceRampHeight(const TerraceParams& p, float run, float d)
{
    const float top = p.baseHeight + p.stepRise * static_cast<float>(p.stepCount);
    if (p.direction == TerraceDirection::Down) {
        if (run <= DIVISION_EPSILON) { return p.baseHeight; }
        return glm::clamp(top - p.stepRise * (d / run - 1.0f), p.baseHeight, top);
    }
    if (run <= DIVISION_EPSILON) { return top; }
    return glm::min(top, p.baseHeight + p.stepRise * (1.0f + d / run));
}

/** Calls emit(const Vec3* polygon, int count) for each planar piece of the ramp surface; the pieces tile the footprint. */
template<typename Emit>
void TerraceRampPieces(const TerraceParams& p, Emit&& emit)
{
    const float run = TerraceRun(p);
    const float n = static_cast<float>(p.stepCount);
    const float hole = TerraceHoleInset(p, run);
    float breaks[3];
    int breakCount = 0;
    if (p.direction == TerraceDirection::Down) {
        breaks[breakCount++] = run;
        breaks[breakCount++] = run * (n + 1.0f);
    }
    else {
        breaks[breakCount++] = run * (n - 1.0f);
    }
    if (!p.bFloor) { breaks[breakCount++] = hole; }

    auto axisLines = [&](float size, bool bNeg, bool bPos, float (&out)[12]) {
        int c = 0;
        out[c++] = 0.0f;
        out[c++] = size;
        if (bNeg) { out[c++] = -p.lipWidth; }
        if (bPos) { out[c++] = size + p.lipWidth; }
        if (bNeg && bPos) { out[c++] = size * 0.5f; }
        for (int i = 0; i < breakCount; ++i) {
            if (bNeg && breaks[i] > 0.0f && breaks[i] < size) { out[c++] = breaks[i]; }
            if (bPos && breaks[i] > 0.0f && breaks[i] < size) { out[c++] = size - breaks[i]; }
        }
        std::sort(out, out + c);
        int u = 0;
        for (int i = 0; i < c; ++i) {
            if (u == 0 || out[i] - out[u - 1] > GEOMETRY_EPSILON) { out[u++] = out[i]; }
        }
        return u;
    };
    float xs[12];
    float zs[12];
    const int nx = axisLines(p.sizeX, (p.sides & TERRACE_SIDE_NEG_X) != 0, (p.sides & TERRACE_SIDE_POS_X) != 0, xs);
    const int nz = axisLines(p.sizeZ, (p.sides & TERRACE_SIDE_NEG_Z) != 0, (p.sides & TERRACE_SIDE_POS_Z) != 0, zs);

    auto lift = [&](const glm::vec2* poly, int count) {
        if (!p.bFloor) {
            glm::vec2 centroid{0.0f};
            for (int i = 0; i < count; ++i) { centroid += poly[i]; }
            centroid /= static_cast<float>(count);
            if (glm::min(TerraceInsetX(p, centroid.x), TerraceInsetZ(p, centroid.y)) > hole) { return; }
        }
        glm::vec3 out[6];
        for (int i = 0; i < count; ++i) {
            const float d = glm::min(TerraceInsetX(p, poly[i].x), TerraceInsetZ(p, poly[i].y));
            out[i] = glm::vec3(poly[i].x, TerraceRampHeight(p, run, glm::max(d, 0.0f)), poly[i].y);
        }
        emit(static_cast<const glm::vec3*>(out), count);
    };

    for (int j = 0; j + 1 < nz; ++j) {
        for (int i = 0; i + 1 < nx; ++i) {
            const glm::vec2 cell[4] = {{xs[i], zs[j]}, {xs[i + 1], zs[j]}, {xs[i + 1], zs[j + 1]}, {xs[i], zs[j + 1]}};
            const glm::vec2 mid = (cell[0] + cell[2]) * 0.5f;
            const bool bBothInset = TerraceInsetX(p, mid.x) < TERRACE_NO_INSET && TerraceInsetZ(p, mid.y) < TERRACE_NO_INSET;
            float s[4];
            bool bNeg = false;
            bool bPos = false;
            for (int c = 0; c < 4; ++c) {
                s[c] = bBothInset ? TerraceInsetX(p, cell[c].x) - TerraceInsetZ(p, cell[c].y) : 0.0f;
                bNeg |= s[c] < -GEOMETRY_EPSILON;
                bPos |= s[c] > GEOMETRY_EPSILON;
            }
            if (!bNeg || !bPos) {
                lift(cell, 4);
                continue;
            }
            for (const float sign : {1.0f, -1.0f}) {
                glm::vec2 piece[6];
                int count = 0;
                for (int c = 0; c < 4; ++c) {
                    const int nc = (c + 1) % 4;
                    const float a = s[c] * sign;
                    const float b = s[nc] * sign;
                    if (a >= 0.0f) { piece[count++] = cell[c]; }
                    if ((a > 0.0f && b < 0.0f) || (a < 0.0f && b > 0.0f)) { piece[count++] = glm::mix(cell[c], cell[nc], a / (a - b)); }
                }
                if (count >= 3) { lift(piece, count); }
            }
        }
    }
}

/** Rectangular pyramid on a base centred at the origin, rising to y = height. topScale 0 is a point; above 0 a flat top that fraction of the base. */
struct PyramidParams
{
    float sizeX{2.0f};
    float sizeZ{2.0f};
    float height{2.0f};
    float topScale{0.0f};
    bool bCapped{true};
    uint8_t _pad0[3]{};

    WILL_REFLECT(PyramidParams, WILL_FIELD(sizeX), WILL_FIELD(sizeZ), WILL_FIELD(height), WILL_FIELD(topScale), WILL_FIELD(bCapped))
};

/** Base corners then top corners (the top collapses to the apex at topScale 0), counter-clockwise from -X -Z. */
inline void PyramidCorners(const PyramidParams& p, glm::vec3 (&out)[8])
{
    const float hx = glm::max(p.sizeX, 0.001f) * 0.5f;
    const float hz = glm::max(p.sizeZ, 0.001f) * 0.5f;
    const float h = glm::max(p.height, 0.001f);
    const float t = glm::clamp(p.topScale, 0.0f, 1.0f);
    const glm::vec2 base[4] = {{-hx, -hz}, {-hx, hz}, {hx, hz}, {hx, -hz}};
    for (int i = 0; i < 4; ++i) {
        out[i] = glm::vec3(base[i].x, 0.0f, base[i].y);
        out[i + 4] = glm::vec3(base[i].x * t, h, base[i].y * t);
    }
}

/** Box sheared up by rise over its length: top and bottom follow the slope, the end faces stay vertical. thickness is vertical. Corner pivot at the low end. */
struct SlantedBeamParams
{
    float sizeX{1.0f};
    float length{2.0f};
    float rise{1.0f};
    float thickness{0.3f};
    float chamferX[4]{};
    float chamferY[4]{};
    float chamferZ[4]{};
    int32_t chamferSegments{1};

    WILL_REFLECT(SlantedBeamParams, WILL_FIELD(sizeX), WILL_FIELD(length), WILL_FIELD(rise), WILL_FIELD(thickness), WILL_FIELD(chamferX), WILL_FIELD(chamferY), WILL_FIELD(chamferZ),
                 WILL_FIELD(chamferSegments))
};

/** Low end corners then high end corners, each counter-clockwise from the pivot looking down +Z. */
inline void SlantedBeamCorners(const SlantedBeamParams& p, glm::vec3 (&out)[8])
{
    const float sx = glm::max(p.sizeX, 0.001f);
    const float len = glm::max(p.length, 0.001f);
    const float t = glm::max(p.thickness, 0.001f);
    const glm::vec2 end[4] = {{0.0f, 0.0f}, {sx, 0.0f}, {sx, t}, {0.0f, t}};
    for (int i = 0; i < 4; ++i) {
        out[i] = glm::vec3(end[i].x, end[i].y, 0.0f);
        out[i + 4] = glm::vec3(end[i].x, end[i].y + p.rise, len);
    }
}

struct PilasterSegment
{
    float height{1.0f};
    float width{0.4f};
    float depth{0.2f};

    WILL_REFLECT(PilasterSegment, WILL_FIELD(height), WILL_FIELD(width), WILL_FIELD(depth))
};

inline constexpr int32_t MAX_PILASTER_SEGMENTS = 8;

/**
 * Wall pier built from stacked segments, bottom first. Pivot at the base centre of the back face; the back face lies on z = 0 and the pier sticks out along +Z.
 * Stepped: each segment is a box. Lofted: each segment tapers from its own size at the bottom to the next segment's size at the top (the last stays straight),
 * so a final zero-height, zero-depth segment ends in a sliver.
 */
struct PilasterParams
{
    PilasterSegment segments[MAX_PILASTER_SEGMENTS]{{1.2f, 0.6f, 0.3f}, {1.2f, 0.45f, 0.2f}, {1.2f, 0.3f, 0.1f}};
    int32_t segmentCount{3};
    bool bLoft{false};
    uint8_t _pad0[3]{};

    WILL_REFLECT(PilasterParams, WILL_FIELD(segments), WILL_FIELD(segmentCount), WILL_FIELD(bLoft))

    static void Sanitize(PilasterParams& p)
    {
        p.segmentCount = glm::clamp(p.segmentCount, 1, MAX_PILASTER_SEGMENTS);
        for (PilasterSegment& s : p.segments) {
            s.height = glm::max(s.height, 0.0f);
            s.width = glm::max(s.width, 0.0f);
            s.depth = glm::max(s.depth, 0.0f);
        }
    }
};

/** Bottom size (x = width, y = depth) and top size of a pilaster segment. */
inline void PilasterSegmentSizes(const PilasterParams& p, int32_t i, glm::vec2& bottom, glm::vec2& top)
{
    const PilasterSegment& s = p.segments[i];
    bottom = {s.width, s.depth};
    top = bottom;
    if (p.bLoft && i + 1 < p.segmentCount) {
        top = {p.segments[i + 1].width, p.segments[i + 1].depth};
    }
}

/**
 * Waffle slab: a flat slab over a grid of downstand beams. Corner pivot like Box; the beam undersides lie on y = 0 and the slab top on y = beamDepth + slabThickness.
 * twinGap > 0 splits every beam into two with a slot of that width up to the slab. arcDegrees > 0 bends it about Z into a barrel vault: the beam undersides keep
 * their length sizeX, the crown stays at x = sizeX / 2 and both springing edges land on y = 0.
 */
struct CofferedSlabParams
{
    float sizeX{6.0f};
    float sizeZ{6.0f};
    float slabThickness{0.2f};
    float beamDepth{0.4f};
    float beamWidth{0.3f};
    int32_t cellsX{4};
    int32_t cellsZ{4};
    float twinGap{0.0f};
    bool bEdgeBeams{true};
    uint8_t _pad0[3]{};
    float arcDegrees{0.0f};
    /** Annulus around the origin instead of a rectangle: cellsX becomes the sector count, cellsZ the ring count, and size and arc are ignored. Concave triangle-mesh collider, static-only. */
    bool bPolar{false};
    /** Ring spacing grows with radius so every coffer stays roughly square. */
    bool bGeometricRings{true};
    uint8_t _pad1[2]{};
    float innerRadius{1.0f};
    float outerRadius{3.0f};
    /** Facets around the circle, radii at the vertices; 0 = round. */
    int32_t polygonSides{0};

    WILL_REFLECT(CofferedSlabParams, WILL_FIELD(sizeX), WILL_FIELD(sizeZ), WILL_FIELD(slabThickness), WILL_FIELD(beamDepth), WILL_FIELD(beamWidth), WILL_FIELD(cellsX), WILL_FIELD(cellsZ),
                 WILL_FIELD(twinGap), WILL_FIELD(bEdgeBeams), WILL_FIELD(arcDegrees), WILL_FIELD(bPolar), WILL_FIELD(bGeometricRings), WILL_FIELD(innerRadius), WILL_FIELD(outerRadius),
                 WILL_FIELD(polygonSides))

    static void Sanitize(CofferedSlabParams& p)
    {
        p.sizeX = glm::max(p.sizeX, 0.01f);
        p.sizeZ = glm::max(p.sizeZ, 0.01f);
        p.slabThickness = glm::max(p.slabThickness, 0.001f);
        p.beamDepth = glm::max(p.beamDepth, 0.0f);
        p.beamWidth = glm::max(p.beamWidth, 0.001f);
        p.cellsX = glm::clamp(p.cellsX, 1, 64);
        p.cellsZ = glm::clamp(p.cellsZ, 1, 64);
        p.twinGap = glm::clamp(p.twinGap, 0.0f, p.beamWidth * 0.9f);
        p.arcDegrees = glm::clamp(p.arcDegrees, 0.0f, 180.0f);
        p.innerRadius = glm::max(p.innerRadius, 0.0f);
        p.outerRadius = glm::max(p.outerRadius, p.innerRadius + 0.01f);
        p.polygonSides = p.polygonSides <= 0 ? 0 : glm::clamp(p.polygonSides, 3, 256);
    }
};

/** Bend of a coffered slab: angle per metre along X (0 = flat) and the radius of the beam undersides. */
struct CofferedSlabArc
{
    float anglePerMetre{0.0f};
    float radius{0.0f};
    float centreX{0.0f};
    float springDrop{0.0f};

    [[nodiscard]] bool IsBent() const { return anglePerMetre > 0.0f; }

    [[nodiscard]] float Angle(float x) const { return (x - centreX) * anglePerMetre; }

    /** Maps a point of the flat slab onto the vault. */
    [[nodiscard]] glm::vec3 Point(const glm::vec3& p) const
    {
        if (!IsBent()) { return p; }
        const float a = Angle(p.x);
        const float r = radius + p.y;
        return {centreX + r * glm::sin(a), r * glm::cos(a) - radius + springDrop, p.z};
    }

    /** Rotates a flat-slab direction at x onto the vault. */
    [[nodiscard]] glm::vec3 Direction(float x, const glm::vec3& d) const
    {
        if (!IsBent()) { return d; }
        const float a = Angle(x);
        const float c = glm::cos(a);
        const float s = glm::sin(a);
        return {d.x * c + d.y * s, -d.x * s + d.y * c, d.z};
    }
};

inline CofferedSlabArc CofferedSlabArcOf(const CofferedSlabParams& p)
{
    CofferedSlabArc arc{};
    const float arcRadians = glm::radians(glm::clamp(p.arcDegrees, 0.0f, 180.0f));
    if (arcRadians < DIRECTION_EPSILON) { return arc; }
    const float sx = glm::max(p.sizeX, 0.01f);
    arc.anglePerMetre = arcRadians / sx;
    arc.radius = sx / arcRadians;
    arc.centreX = 0.5f * sx;
    arc.springDrop = arc.radius * (1.0f - glm::cos(0.5f * arcRadians));
    return arc;
}

/** Flat-slab X subdivisions so no bent facet spans more than ~3 degrees. */
inline int32_t CofferedSlabArcSegments(const CofferedSlabParams& p)
{
    return p.arcDegrees > 0.0f ? glm::max(1, static_cast<int32_t>(glm::ceil(glm::clamp(p.arcDegrees, 0.0f, 180.0f) / 3.0f))) : 1;
}

/** Beam bands along one axis as [start, end] pairs (twin beams give two per line), clipped to [0, size]. Returns the pair count. */
inline int32_t CofferedSlabBeamBands(const CofferedSlabParams& p, float size, int32_t cells, glm::vec2* out, int32_t maxOut)
{
    const float halfW = p.beamWidth * 0.5f;
    const float halfGap = glm::min(p.twinGap, p.beamWidth * 0.9f) * 0.5f;
    int32_t count = 0;
    for (int32_t i = 0; i <= cells && count + 2 <= maxOut; ++i) {
        if (!p.bEdgeBeams && (i == 0 || i == cells)) { continue; }
        const float c = size * static_cast<float>(i) / static_cast<float>(cells);
        const glm::vec2 band{glm::max(c - halfW, 0.0f), glm::min(c + halfW, size)};
        if (halfGap > 0.0f) {
            if (c - halfGap > band.x) { out[count++] = {band.x, c - halfGap}; }
            if (c + halfGap < band.y) { out[count++] = {c + halfGap, band.y}; }
        }
        else if (band.y > band.x) {
            out[count++] = band;
        }
    }
    return count;
}

inline constexpr int32_t MAX_COFFERED_BANDS = 2 * (64 + 1);

inline constexpr int32_t MAX_PROCEDURAL_REPEAT = 256;
inline constexpr int32_t MAX_PROCEDURAL_REPEAT_TOTAL = 1024;

/** Repeats a procedural shape along up to three axes, each copy moved by that axis' offset from the last. Applies to the mesh and its collider; all counts 1 = off. */
struct ProceduralRepeat
{
    int32_t count{1};
    Vec3 offset{1.0f, 0.0f, 0.0f};
    int32_t count2{1};
    Vec3 offset2{0.0f, 0.0f, 1.0f};
    int32_t count3{1};
    Vec3 offset3{0.0f, 1.0f, 0.0f};

    WILL_REFLECT(ProceduralRepeat, WILL_FIELD(count), WILL_FIELD(offset), WILL_FIELD(count2), WILL_FIELD(offset2), WILL_FIELD(count3), WILL_FIELD(offset3))

    static void Sanitize(ProceduralRepeat& r)
    {
        r.count = glm::clamp(r.count, 1, MAX_PROCEDURAL_REPEAT);
        r.count2 = glm::clamp(r.count2, 1, MAX_PROCEDURAL_REPEAT);
        r.count3 = glm::clamp(r.count3, 1, MAX_PROCEDURAL_REPEAT);
    }

    [[nodiscard]] bool IsActive() const { return count > 1 || count2 > 1 || count3 > 1; }

    /** Copy count, capped at MAX_PROCEDURAL_REPEAT_TOTAL (later copies along the third axis drop first). */
    [[nodiscard]] int32_t Total() const
    {
        const int32_t a = glm::clamp(count, 1, MAX_PROCEDURAL_REPEAT);
        const int32_t b = glm::clamp(count2, 1, MAX_PROCEDURAL_REPEAT);
        const int32_t c = glm::clamp(count3, 1, MAX_PROCEDURAL_REPEAT);
        return glm::min(a * b * c, MAX_PROCEDURAL_REPEAT_TOTAL);
    }

    /** Offset of copy k, 0 <= k < Total(); the first axis varies fastest. */
    [[nodiscard]] Vec3 Offset(int32_t k) const
    {
        const int32_t a = glm::clamp(count, 1, MAX_PROCEDURAL_REPEAT);
        const int32_t b = glm::clamp(count2, 1, MAX_PROCEDURAL_REPEAT);
        return offset * static_cast<float>(k % a) + offset2 * static_cast<float>((k / a) % b) + offset3 * static_cast<float>(k / (a * b));
    }
};

using ProceduralParams = std::variant<std::monostate, StaircaseParams, BoxParams, CylinderParams, CapsuleParams, TorusParams, ArchParams, WedgeParams, ConeParams, DoorParams, PlaneParams, SphereParams
    , SubdividedSphereParams, HemisphereParams, PipeParams, TetrahedronParams, OctahedronParams, IcosahedronParams, DodecahedronParams, KleinBottleParams, TrefoilKnotParams, CurvedRampParams, BowlParams, SpiralStaircaseParams, RingParams, WallParams, LatticeParams, CorrugatedPanelParams
    , TerraceParams, PyramidParams, SlantedBeamParams, PilasterParams, CofferedSlabParams>;

inline constexpr int32_t MAX_MODULE_PARTS = 32;
inline constexpr int32_t MAX_MODULE_SLOTS = 8;

struct ModulePart
{
    ProceduralParams shape{};
    Vec3 offset{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    int32_t materialSlot{0};

    WILL_REFLECT(ModulePart, WILL_FIELD(shape, .key = "type"), WILL_FIELD(offset), WILL_FIELD(rotation), WILL_FIELD(materialSlot, .key = "slot"))

    static void Sanitize(ModulePart& p) { p.materialSlot = glm::clamp(p.materialSlot, 0, MAX_MODULE_SLOTS - 1); }
};

/**
 * A kit piece: several procedural shapes baked into one model.
 */
struct ModuleParams
{
    Core::InlineVector<ModulePart, MAX_MODULE_PARTS> parts;

    WILL_REFLECT(ModuleParams, WILL_FIELD(parts))
};

/** Horizontal placement of each line relative to the model origin. */
enum class Text3DAlign : uint8_t
{
    Left = 0,
    Center = 1,
    Right = 2,
};

/** Vertical placement of the text block relative to the model origin. Baseline puts the first line's baseline at y=0. */
enum class Text3DAnchor : uint8_t
{
    Baseline = 0,
    Top = 1,
    Center = 2,
    Bottom = 3,
};

/**
 * Input for an extruded 3D-text model.
 * `depth`/`scale`/`flatness` are consumed at generation time; `flatness` is an EM-space tolerance.
 * `text` breaks lines on '\n'; line spacing comes from the font's lineHeight.
 * `wrapWidth` is in world units (post-scale); 0 disables word wrapping.
 * `bendRadius` wraps the laid-out text around a vertical cylinder of that radius (world units; positive bulges toward +Z, negative concave); 0 disables.
 */
struct Text3DParams
{
    // Identity (hashed for dedup)
    uint64_t fontId{0};
    Core::InlineString<256> text{};
    float depth{0.2f};
    float flatness{0.005f};
    float tracking{0.0f};
    float scale{1.0f};
    float wrapWidth{0.0f};
    float bendRadius{0.0f};
    bool bSmoothNormals{true};
    Text3DAlign align{Text3DAlign::Left};
    Text3DAnchor anchor{Text3DAnchor::Baseline};

    // Font the generation job reads; kept alive by the requesting component (not a model-owned ref, so font hot-reload can evict cleanly).
    const Font* font{nullptr};
};
} // Render

#endif //WILL_ENGINE_MODEL_TYPES_H
