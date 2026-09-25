//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_SHADER_INTEROP_H
#define WILLENGINEV3_SHADER_INTEROP_H

#ifdef __SLANG__
#define SHADER_PUBLIC public
#define SHADER_ENUM enum
#define SHADER_CONST const static
#define SHADER_PTR(T) T*
#else
#include <glm/glm.hpp>
#include "core/types/math.h"
#include <cstdint>

using uint = uint32_t;
using int32 = int32_t;
using uint32 = uint32_t;

using float2 = Vec2;
using float3 = Vec3;
using float4 = Vec4;

using int2 = IVec2;
using int3 = IVec3;
using int4 = IVec4;

using uint2 = UVec2;
using uint3 = UVec3;
using uint4 = UVec4;

using float2x2 = Mat2;
using float3x3 = Mat3;
using float4x4 = Mat4;

#define SHADER_PUBLIC
#define SHADER_ENUM enum class
#define SHADER_CONST constexpr inline
#define SHADER_PTR(T) VkDeviceAddress
#endif // __SLANG__

SHADER_PUBLIC SHADER_ENUM DebugTransformationType
{
    None = 0,
    DepthRemap = 1,
    MultiplyBy1000 = 2,
    MultiplyBy10000 = 3,
    DivideBy1000 = 4,
    DivideBy10000 = 5,
    StencilRemap = 6,
    VisBuffTriangle = 7,
    VisBuffMeshlet = 8,
    VisBuffInstance = 9,
    GBufferNormal = 10,
    GBufferMotionVectors = 11,
    GBufferPBR = 12,
    GBufferAlbedo = 13,
    GBufferEmissive = 14,
    VisBucketShading = 15,
    VisBucketLighting = 16,
    ReservoirLightIdx = 17,
    ReservoirTemporalLightIdx = 18,
    ReservoirTemporalW = 19,
    ReservoirGenerateW = 20,
    ReservoirSpatialLightIdx = 21,
    ReservoirSpatialW = 22,
    ReservoirHistoryLightIdx = 23,
    ReservoirHistoryW = 24,
    ViewSpacePosition = 25,
    NdotV = 26,
    GBufferViewZDelta = 27,
    SunShadowVisibility = 28,
    SunShadowPenumbra = 29,
    SunShadowTiles = 30,
    SunShadowTileValue = 31,
    YCoCgSignal = 32,
    ReblurInternalData = 33,
    ReblurData2 = 34,
    BentNormal = 35,
    ReflectionProbeIndex = 40,
    ReflectionProbeBinDisagreement = 41,
    DofCoc = 42,
    DofTileMax = 43,
    AlphaOnly = 44,
    DofZones = 45,
    GTAOTemporalAO = 46,
    GTAOTemporalCount = 47,
    GTAOResolved = 48,
    ReGIRCell = 49,
    ReGIRCellMass = 50,
    ReGIRCursorCell = 51,
    Tonemap = 52,
};

SHADER_PUBLIC struct Frustum
{
    SHADER_PUBLIC float4 planes[6];
};

SHADER_PUBLIC struct SceneData
{
    SHADER_PUBLIC float4x4 view;
    SHADER_PUBLIC float4x4 proj;
    SHADER_PUBLIC float4x4 viewProj;
    SHADER_PUBLIC float4x4 invView;
    SHADER_PUBLIC float4x4 invProj;
    SHADER_PUBLIC float4x4 invViewProj;
    SHADER_PUBLIC float4x4 prevView;
    SHADER_PUBLIC float4x4 prevProj;
    SHADER_PUBLIC float4x4 prevViewProj;
    SHADER_PUBLIC float4x4 clipToPrevClip; // currClip -> prevClip space in 1 single multiplication
    SHADER_PUBLIC float4x4 invPrevView;

    SHADER_PUBLIC Frustum frustum;

    SHADER_PUBLIC float4 clipPlane; // near clip plane to define clip distance
    SHADER_PUBLIC float4 cameraWorldPos;

    SHADER_PUBLIC float2 jitter;
    SHADER_PUBLIC float2 prevJitter;

    SHADER_PUBLIC float2 mainRenderTargetSize;
    SHADER_PUBLIC float2 texelSize;

    SHADER_PUBLIC float2 ndcToViewMul;
    SHADER_PUBLIC float2 ndcToViewAdd;

    SHADER_PUBLIC float2 ndcToViewMulXPixelSize;
    SHADER_PUBLIC float uvDerivativeScale;
    SHADER_PUBLIC float preExposure;


    SHADER_PUBLIC float depthLinearizeMult;
    SHADER_PUBLIC float depthLinearizeAdd;
    SHADER_PUBLIC float deltaTime;
    SHADER_PUBLIC float lodScreenSizeScale;

    SHADER_PUBLIC float prevPreExposure;
    SHADER_PUBLIC float framerateScale;
    SHADER_PUBLIC float _pad1;
    SHADER_PUBLIC float _pad2;
};

SHADER_PUBLIC struct ReadbackStruct
{
    SHADER_PUBLIC uint32_t meshletCount;
    SHADER_PUBLIC uint32_t shadingDispatches;
    SHADER_PUBLIC uint32_t lightingDispatches;
    SHADER_PUBLIC uint32_t _pad0;
    SHADER_PUBLIC uint64_t selectedStableId;
    SHADER_PUBLIC uint32_t wcOccupied;
    SHADER_PUBLIC uint32_t wcCarried;
    SHADER_PUBLIC uint32_t wcEvicted;
    SHADER_PUBLIC uint32_t wcInsertsFailed;
    SHADER_PUBLIC uint32_t wcShaded;
    SHADER_PUBLIC uint32_t culledInstanceFrustum;
    SHADER_PUBLIC uint32_t culledInstanceContribution;
    SHADER_PUBLIC uint32_t culledInstanceOcclusion;
    SHADER_PUBLIC uint32_t culledMeshletFrustum;
    SHADER_PUBLIC uint32_t culledMeshletCone;
    SHADER_PUBLIC uint32_t culledMeshletContribution;
    SHADER_PUBLIC uint32_t culledMeshletOcclusion;
    SHADER_PUBLIC uint32_t meshletRegionExpanded[4];
    SHADER_PUBLIC uint32_t meshletRegionVisible[4];
    SHADER_PUBLIC float adaptedLuminance;
    SHADER_PUBLIC uint32_t regirActiveCells;
    SHADER_PUBLIC uint32_t regirInsertsFailed;
    SHADER_PUBLIC uint32_t regirGatherOverflow;
    SHADER_PUBLIC uint32_t regirConeRejected;
    // Top 8 entries by mass share.
    SHADER_PUBLIC uint32_t regirCursorValid;
    SHADER_PUBLIC uint32_t regirCursorLevel;
    SHADER_PUBLIC int32_t regirCursorCell[3];
    SHADER_PUBLIC uint32_t regirCursorSlot;
    SHADER_PUBLIC uint32_t regirCursorEntryCount;
    SHADER_PUBLIC float regirCursorTotalMass;
    SHADER_PUBLIC uint32_t regirCursorTopKey[8];
    SHADER_PUBLIC float regirCursorTopShare[8];
    SHADER_PUBLIC uint32_t regirCursorTopLightCount[8];
    SHADER_PUBLIC float regirCursorTopPos[24];
    SHADER_PUBLIC uint32_t _pad1;
    // Kept = what binning listed, inRange = everything passing its membership test.
    SHADER_PUBLIC uint32_t wgCursorValid;
    SHADER_PUBLIC uint32_t wgCursorLevel;
    SHADER_PUBLIC uint32_t wgCursorCell[3];
    SHADER_PUBLIC uint32_t wgCursorFlatIndex;
    SHADER_PUBLIC float wgCursorAabbMin[3];
    SHADER_PUBLIC float wgCursorAabbMax[3];
    SHADER_PUBLIC uint32_t wgCursorAnalyticKept;
    SHADER_PUBLIC uint32_t wgCursorAnalyticInRange;
    SHADER_PUBLIC float wgCursorAnalyticPower;
    SHADER_PUBLIC uint32_t wgCursorMeshletKept;
    SHADER_PUBLIC uint32_t wgCursorMeshletInRange;
    SHADER_PUBLIC float wgCursorMeshletPower;
    SHADER_PUBLIC uint32_t wgCursorTopLightIdx[8];
    SHADER_PUBLIC uint32_t wgCursorTopLightType[8];
    SHADER_PUBLIC float wgCursorTopLightPower[8];
    SHADER_PUBLIC float wgCursorTopLightRange[8];
    SHADER_PUBLIC float wgCursorTopLightPos[24];
    SHADER_PUBLIC uint32_t wgCursorTopMeshletIdx[8];
    SHADER_PUBLIC uint32_t wgCursorTopMeshletLightCount[8];
    SHADER_PUBLIC float wgCursorTopMeshletPower[8];
    SHADER_PUBLIC float wgCursorTopMeshletCenter[24];
    SHADER_PUBLIC uint32_t pickValid;
    SHADER_PUBLIC uint32_t pickRequestId;
    SHADER_PUBLIC uint32_t pickInstanceIndex;
    SHADER_PUBLIC uint32_t pickMeshletIndex;
    SHADER_PUBLIC uint32_t pickTriangleIndex;
    SHADER_PUBLIC float pickViewDepth;
    SHADER_PUBLIC float pickWorldPos[3];
};

SHADER_PUBLIC struct DrawMeshTasksIndirectCommand
{
    SHADER_PUBLIC uint32_t groupCountX;
    SHADER_PUBLIC uint32_t groupCountY;
    SHADER_PUBLIC uint32_t groupCountZ;
};

#endif // WILLENGINEV3_SHADER_INTEROP_H
