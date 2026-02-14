//
// Created by William on 2026-02-14.
//

#ifndef WILL_ENGINE_INSTANCED_GEOMETRY_PASS_H
#define WILL_ENGINE_INSTANCED_GEOMETRY_PASS_H
#include <string>

namespace Render
{
class PipelineManager;
class RenderGraph;

struct InstancedGeometryPassConfig
{
    std::string prefix;
    uint32_t instanceCount;
    uint32_t visibleMeshletUpperBound;
    bool bClearTargets;

    // Buffer sizes
    size_t instanceMeshletOffsetsBufferSize;
    size_t level1SumsBufferSize;
    size_t level1BlockSumsBufferSize;
    size_t level2SumsBufferSize;
    size_t level2BlockSumsBufferSize;
    size_t scannedLevel2BlockSumsBufferSize;
    size_t intermediateMeshletBufferSize;
    size_t meshletLevel1SumsBufferSize;
    size_t meshletLevel1BlockSumsBufferSize;
    size_t meshletLevel2SumsBufferSize;
    size_t meshletLevel2BlockSumsBufferSize;
    size_t meshletScannedLevel2BlockSumsBufferSize;
    size_t visibleMeshletsBufferSize;

    int32_t lodBias = 0;
};

struct InstancedGeometryPassOutputs
{
    std::string visibleMeshlets;
    std::string compactedDispatchArgs;

    // For readback
    std::string meshletCountDispatchArgs;
};

InstancedGeometryPassOutputs SetupInstancedGeometryPass(RenderGraph& graph, const InstancedGeometryPassConfig& config, PipelineManager* pipelineManager);
} // Render

#endif //WILL_ENGINE_INSTANCED_GEOMETRY_PASS_H
