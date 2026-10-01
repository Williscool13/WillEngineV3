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
    uint32_t normalOct; // snorm8: bits[7:0]=X, bits[15:8]=Y
    uint32_t tangentOct; // snorm8: bits[7:0]=X, bits[15:8]=Y; bit[16]=sign(0=neg,1=pos)
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

    WILL_REFLECT(StaircaseParams, WILL_FIELD(stepCount), WILL_FIELD(width), WILL_FIELD(totalDepth), WILL_FIELD(totalHeight), WILL_FIELD(bSpecifyStepHeight),
                 WILL_FIELD(stepHeight), WILL_FIELD(bIsClosed), WILL_FIELD(bSpecifyStepDepth), WILL_FIELD(stepDepth), WILL_FIELD(slabThickness),
                 WILL_FIELD(sideWallHeight), WILL_FIELD(sideWallThickness), WILL_FIELD(sideWallStart), WILL_FIELD(bSideWallNegX), WILL_FIELD(bSideWallPosX),
                 WILL_FIELD(stepChamfer))
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
    return StaircaseStepHeight(p) * z / glm::max(stepDepth, 1e-6f);
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
        const float run = StaircaseTotalDepth(p) / static_cast<float>(glm::max(p.stepCount, 1)) / glm::max(StaircaseStepHeight(p), 1e-6f);
        const float kinks[2] = {p.slabThickness * run, p.totalHeight * run};
        for (const float kink : kinks) {
            if (kink > out[count - 1] + 1e-5f && kink < z1 - 1e-5f) { out[count++] = kink; }
        }
    }
    out[count++] = z1;
    return count;
}

struct BoxParams
{
    float sizeX{1.0f}, sizeY{1.0f}, sizeZ{1.0f};
    float chamferX[4]{};
    float chamferY[4]{};
    float chamferZ[4]{};

    WILL_REFLECT(BoxParams, WILL_FIELD(sizeX), WILL_FIELD(sizeY), WILL_FIELD(sizeZ), WILL_FIELD(chamferX), WILL_FIELD(chamferY), WILL_FIELD(chamferZ))
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

    WILL_REFLECT(WedgeParams, WILL_FIELD(sizeX), WILL_FIELD(sizeY), WILL_FIELD(sizeZ), WILL_FIELD(baseHeight), WILL_FIELD(chamferX), WILL_FIELD(chamferY), WILL_FIELD(chamferZ),
                 WILL_FIELD(slabThickness))
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
    bool bCapped{true};
    uint8_t _pad0[3]{};

    WILL_REFLECT(ConeParams, WILL_FIELD(radius), WILL_FIELD(height), WILL_FIELD(slices), WILL_FIELD(bCapped))
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

    WILL_REFLECT(HemisphereParams, WILL_FIELD(radius), WILL_FIELD(slices), WILL_FIELD(stacks))
};

struct PipeParams
{
    float outerRadius{0.5f};
    float innerRadius{0.3f};
    float height{2.0f};
    int32_t slices{16};

    WILL_REFLECT(PipeParams, WILL_FIELD(outerRadius), WILL_FIELD(innerRadius), WILL_FIELD(height), WILL_FIELD(slices))
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
};

struct SplineProfile
{
    SplineProfileType type{SplineProfileType::Tube};
    float width{0.4f};
    float height{0.4f};
    float cornerRadius{0.08f};
    int32_t cornerSegments{3};
    float thickness{0.05f};

    WILL_REFLECT(SplineProfile, WILL_FIELD(type), WILL_FIELD(width), WILL_FIELD(height), WILL_FIELD(cornerRadius), WILL_FIELD(cornerSegments), WILL_FIELD(thickness))
};

struct SplineRailing
{
    bool bEnabled{false};
    Core::InlineVector<Vec2, 8> lanes{};
    bool bPosts{true};
    int32_t postInterval{4};
    float postBottom{0.0f};
    float postTop{1.0f};
    Vec2 postSize{0.05f, 0.05f};
    float postLateral{0.0f};
    float lateralOffset{0.0f};

    WILL_REFLECT(SplineRailing, WILL_FIELD(bEnabled), WILL_FIELD(lanes), WILL_FIELD(bPosts), WILL_FIELD(postInterval), WILL_FIELD(postBottom), WILL_FIELD(postTop),
                 WILL_FIELD(postSize), WILL_FIELD(postLateral), WILL_FIELD(lateralOffset))
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

    WILL_REFLECT(SplineParams, WILL_FIELD(spline), WILL_FIELD(radius), WILL_FIELD(rollAngle), WILL_FIELD(sides), WILL_FIELD(segmentsPerSpan), WILL_FIELD(bCaps),
                 WILL_FIELD(bCrossPlanks), WILL_FIELD(crossPlankInterval), WILL_FIELD(crossPlankHeight), WILL_FIELD(crossPlankThickness), WILL_FIELD(crossPlankLength),
                 WILL_FIELD(profile), WILL_FIELD(railing))
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
        if (run <= 1e-6f) { return p.baseHeight; }
        return glm::clamp(top - p.stepRise * (d / run - 1.0f), p.baseHeight, top);
    }
    if (run <= 1e-6f) { return top; }
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
            if (u == 0 || out[i] - out[u - 1] > 1e-5f) { out[u++] = out[i]; }
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
                bNeg |= s[c] < -1e-6f;
                bPos |= s[c] > 1e-6f;
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

    WILL_REFLECT(SlantedBeamParams, WILL_FIELD(sizeX), WILL_FIELD(length), WILL_FIELD(rise), WILL_FIELD(thickness), WILL_FIELD(chamferX), WILL_FIELD(chamferY), WILL_FIELD(chamferZ))
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

using ProceduralParams = std::variant<std::monostate, StaircaseParams, BoxParams, CylinderParams, CapsuleParams, TorusParams, ArchParams, WedgeParams, ConeParams, DoorParams, PlaneParams, SphereParams
    , SubdividedSphereParams, HemisphereParams, PipeParams, TetrahedronParams, OctahedronParams, IcosahedronParams, DodecahedronParams, KleinBottleParams, TrefoilKnotParams, CurvedRampParams, BowlParams, SpiralStaircaseParams, RingParams, WallParams, LatticeParams, CorrugatedPanelParams
    , TerraceParams, PyramidParams, SlantedBeamParams>;

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
