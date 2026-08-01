//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_TYPES_H
#define WILL_ENGINE_MODEL_TYPES_H

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

    Vec3 boundingBoxMin{};
    Vec3 boundingBoxMax{};
    Vec4 boundingSphere{};

    // Raytracing
    uint64_t blasHandle{0};
    uint64_t blasDeviceAddress{0};
    OffsetAllocator::Allocation blasAllocation{};
};

struct MeshInformation
{
    Core::InlineString<> name;
    // todo parameterize this 128 primitive per mesh limit. Perhaps even increase it.
    Core::InlineVector<PrimitiveProperty, 128> primitiveProperties;
};

/**
 * Local-space emissive triangles of one primitive, extracted at load for ReSTIR light gathering.
 * verts holds 3 entries per triangle (v0, e1 = v1-v0, e2 = v2-v0) in BLAS index order so a RayQuery
 * PrimitiveIndex() equals the triangle index. bTruncated marks a tail cut by the extraction cap. truncated sets never get a BRDF-ray light-index mapping (base + PrimitiveIndex() would misalign).
 */
struct EmissiveTriangleSet
{
    // Global mega-buffer primitive index (post-rebase, matches MeshPrimitiveInstance.primitiveIndex).
    uint32_t primitiveIndex{~0u};
    bool bTruncated{false};
    Core::HeapArray<Vec3> verts{};
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

/*
// Remove heap when implementing
struct AnimationSampler
{
    enum class Interpolation
    {
        Linear,
        Step,
        CubicSpline,
    };

    std::vector<float> timestamps;
    std::vector<float> values;
    Interpolation interpolation;
};

struct AnimationChannel
{
    enum class TargetPath
    {
        Translation,
        Rotation,
        Scale,
        Weights,
    };

    uint32_t samplerIndex;
    uint32_t targetNodeIndex;
    TargetPath targetPath;
};

struct Animation
{
    std::string name;
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;
    float duration;
};*/

struct StaticModelData
{
    Core::HeapArray<MeshInformation> meshes{};
    Core::HeapArray<Node> nodes{};
    Core::HeapArray<Material> materials{};
    Core::HeapArray<EmissiveTriangleSet> emissiveTriangles{};

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
};

struct BoxParams
{
    float sizeX{1.0f}, sizeY{1.0f}, sizeZ{1.0f};
};

struct CylinderParams
{
    float radius{0.5f};
    float height{2.0f};
    int32_t slices{16};
    bool bCapped{true};
    uint8_t _pad0[3]{};
};

struct CapsuleParams
{
    float radius{0.5f};
    float height{2.0f};
    int32_t slices{16};
    int32_t rings{8};
};

struct TorusParams
{
    float ringRadius{1.0f};
    float tubeRadius{0.25f};
    int32_t slices{16};
    int32_t stacks{16};
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
};

struct WedgeParams
{
    float sizeX{1.0f};
    float sizeY{1.0f};
    float sizeZ{1.0f};
};

struct ConeParams
{
    float radius{0.5f};
    float height{2.0f};
    int32_t slices{16};
    bool bCapped{true};
    uint8_t _pad0[3]{};
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
};

struct PlaneParams
{
    float sizeX{2.0f};
    float sizeZ{2.0f};
    int32_t tilesX{1};
    int32_t tilesZ{1};
};

struct SphereParams
{
    float radius{0.5f};
    int32_t slices{16};
    int32_t stacks{8};
};

struct SubdividedSphereParams
{
    float radius{0.5f};
    int32_t subdivisions{3};
};

struct HemisphereParams
{
    float radius{0.5f};
    int32_t slices{16};
    int32_t stacks{8};
};

struct PipeParams
{
    float outerRadius{0.5f};
    float innerRadius{0.3f};
    float height{2.0f};
    int32_t slices{16};
};

struct TetrahedronParams
{
    float radius{0.5f};
};

struct OctahedronParams
{
    float radius{0.5f};
};

struct IcosahedronParams
{
    float radius{0.5f};
};

struct DodecahedronParams
{
    float radius{0.5f};
};

struct KleinBottleParams
{
    float scale{1.0f};
    int32_t slices{8};
    int32_t stacks{8};
};

struct TrefoilKnotParams
{
    float scale{1.0f};
    float tubeRadius{1.0f};
    int32_t slices{16};
    int32_t stacks{128};
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
};

struct WallOpening
{
    float x{0.0f};
    float y{0.0f};
    float w{0.0f};
    float h{0.0f};
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
};

/**
 * Corrugated sheet: flat back at z=0, web sizeZ thick, ribCount trapezoidal ribs protruding to
 * z = sizeZ + ribDepth, evenly pitched across X and running the full Y. Each rib is a plateau of
 * ribWidth with 45-degree flanks (flank run = ribDepth, steepened when the pitch cannot fit them).
 * Corner pivot like BoxParams.
 */
struct CorrugatedPanelParams
{
    float sizeX{2.4f};
    float sizeY{2.4f};
    float sizeZ{0.05f};
    float ribDepth{0.05f};
    float ribWidth{0.2f};
    int32_t ribCount{6};
};

using ProceduralParams = std::variant<std::monostate, StaircaseParams, BoxParams, CylinderParams, CapsuleParams, TorusParams, ArchParams, WedgeParams, ConeParams, DoorParams, PlaneParams, SphereParams
    , SubdividedSphereParams, HemisphereParams, PipeParams, TetrahedronParams, OctahedronParams, IcosahedronParams, DodecahedronParams, KleinBottleParams, TrefoilKnotParams, CurvedRampParams, BowlParams, SpiralStaircaseParams, RingParams, WallParams, LatticeParams, CorrugatedPanelParams>;

inline constexpr int32_t MAX_MODULE_PARTS = 32;
inline constexpr int32_t MAX_MODULE_SLOTS = 8;

struct ModulePart
{
    ProceduralParams shape{};
    Vec3 offset{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    int32_t materialSlot{0};
};

/**
 * A kit piece: several procedural shapes baked into one model.
 */
struct ModuleParams
{
    Core::InlineVector<ModulePart, MAX_MODULE_PARTS> parts;
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
