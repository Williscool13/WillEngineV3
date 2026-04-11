//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_TYPES_H
#define WILL_ENGINE_MODEL_TYPES_H
#include <vector>
#include <variant>

#include "engine/spline/spline.h"

#include "offsetAllocator.hpp"
#include "core/containers/heap_array.h"
#include "engine/resources/material/material.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "core/containers/vector.h"
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
};

struct MeshInformation
{
    Core::InlineString<> name;
    // todo parameterize this 128 primitive per mesh limit. Perhaps even increase it.
    Core::InlineVector<PrimitiveProperty,128> primitiveProperties;
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

// Remove heap when implementing
/*struct AnimationChannel
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

    OffsetAllocator::Allocation vertexAllocation{};
    // todo index allocation for RT
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

struct StaircaseParams
{
    int32_t stepCount{10};
    float width{1.0f};
    float totalDepth{3.0f};
    float totalHeight{2.0f};
    bool bSpecifyStepHeight{false};
    float stepHeight{0.2f};
    bool bIsClosed{true};
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

struct SplineParams
{
    Spline spline;
    float radius{0.5f};
    float rollAngle{0.0f};
    int32_t sides{8};
    int32_t segmentsPerSpan{8};
    bool bCaps{true};
    bool bDualPath{false};
    float dualPathSpacing{1.0f};
    bool bCrossPlanks{false};
    int32_t crossPlankInterval{4};
    float crossPlankHeight{0.0f};
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
    float flatLength{1.0f};
    float lipHeight{0.02f};
};

using ProceduralParams = std::variant<std::monostate, StaircaseParams, BoxParams, CylinderParams, CapsuleParams, TorusParams, ArchParams, WedgeParams, ConeParams, DoorParams, PlaneParams, SphereParams
    , SubdividedSphereParams, HemisphereParams, PipeParams, TetrahedronParams, OctahedronParams, IcosahedronParams, DodecahedronParams, KleinBottleParams, TrefoilKnotParams, CurvedRampParams, BowlParams>;
} // Render

#endif //WILL_ENGINE_MODEL_TYPES_H
