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
    uint32_t instanceCount{0};
    uint32_t instanceBufferOffset{0};
    uint32_t visibleMeshletUpperBound{128};

    // Buffer sizes
    size_t instanceMeshletOffsetsBufferSize{128};
    size_t level1SumsBufferSize{128};
    size_t level1BlockSumsBufferSize{128};
    size_t level2SumsBufferSize{128};
    size_t level2BlockSumsBufferSize{128};
    size_t scannedLevel2BlockSumsBufferSize{128};
    size_t intermediateMeshletBufferSize{128};
    size_t meshletLevel1SumsBufferSize{128};
    size_t meshletLevel1BlockSumsBufferSize{128};
    size_t meshletLevel2SumsBufferSize{128};
    size_t meshletLevel2BlockSumsBufferSize{128};
    size_t meshletScannedLevel2BlockSumsBufferSize{128};
    size_t visibleMeshletsBufferSize{128};

    int32_t lodBias{0};
};

struct InstancedGeometryPassOutputs
{
    std::string visibleMeshlets;
    std::string compactedDispatchArgs;

    // For readback
    std::string meshletCountDispatchArgs;
};

InstancedGeometryPassOutputs SetupInstancedGeometryPass(RenderGraph& graph, const InstancedGeometryPassConfig& config, PipelineManager* pipelineManager, uint32_t sceneDataIndex);

/**
 *
 * @param graph
 * @param config
 * @param pipelineManager
 * @param sceneDataIndex
 * @param cascadeIndex
 * @param bAllowBufferAliasing Disable aliasing to allow the shadow passes to happen concurrently with each other and the main pass
 * @return
 */
InstancedGeometryPassOutputs SetupInstancedGeometryShadowPass(RenderGraph& graph, const InstancedGeometryPassConfig& config, PipelineManager* pipelineManager, uint32_t sceneDataIndex, uint32_t cascadeIndex, bool bAllowBufferAliasing);
} // Render

#endif //WILL_ENGINE_INSTANCED_GEOMETRY_PASS_H
