//
// Created by William on 2026-01-21.
//

#include "render_view_helpers.h"

#include "core/containers/arena_fixed_map.h"
#include "core/math/math_helpers.h"
#include "render/interface/render_interface.h"
#include "render/frame_resources.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/types/render_types.h"

namespace Render
{
SceneData GenerateSceneData(const Core::RenderView& view, Core::AntiAliasingMode aaMode, Core::Array<uint32_t, 2> renderExtent, uint64_t frameNumber, float deltaTime)
{
    const glm::mat4 viewMatrix = view.currentViewData.view;
    const glm::mat4 projMatrix = view.currentViewData.proj;

    const glm::mat4 prevViewMatrix = view.previousViewData.view;
    const glm::mat4 prevProjMatrix = view.previousViewData.proj;

    SceneData sceneData{};
    sceneData.view = viewMatrix;
    sceneData.prevView = prevViewMatrix;

    if (aaMode == Core::AntiAliasingMode::TAA) {
        glm::mat4 jitteredProj = projMatrix;
        float haltonX = 2.0f * Halton((frameNumber + 1) % HALTON_SEQUENCE_COUNT + 1, 2) - 1.0f;
        float haltonY = 2.0f * Halton((frameNumber + 1) % HALTON_SEQUENCE_COUNT + 1, 3) - 1.0f;
        float jitterX = haltonX * (1.0f / renderExtent[0]);
        float jitterY = haltonY * (1.0f / renderExtent[1]);
        jitteredProj[2][0] += jitterX;
        jitteredProj[2][1] += jitterY;

        glm::mat4 jitteredPrevProj = prevProjMatrix;
        float prevHaltonX = 2.0f * Halton((frameNumber) % HALTON_SEQUENCE_COUNT + 1, 2) - 1.0f;
        float prevHaltonY = 2.0f * Halton((frameNumber) % HALTON_SEQUENCE_COUNT + 1, 3) - 1.0f;
        float prevJitterX = prevHaltonX * (1.0f / renderExtent[0]);
        float prevJitterY = prevHaltonY * (1.0f / renderExtent[1]);
        jitteredPrevProj[2][0] += prevJitterX;
        jitteredPrevProj[2][1] += prevJitterY;

        sceneData.jitter = {jitterX, jitterY};
        sceneData.prevJitter = {prevJitterX, prevJitterY};
        sceneData.proj = jitteredProj;
        sceneData.prevProj = jitteredPrevProj;
    }
    else if (aaMode == Core::AntiAliasingMode::SMAAT2X) {
        // Alternates between two canonical SMAA subsample positions each frame.
        static constexpr glm::vec2 kSubsampleOffsets[2] = {{-0.25f, -0.25f}, {0.25f, 0.25f}};
        const glm::vec2& curr = kSubsampleOffsets[frameNumber % 2];
        const glm::vec2& prev = kSubsampleOffsets[(frameNumber + 1) % 2];

        float jitterX = curr.x / static_cast<float>(renderExtent[0]);
        float jitterY = curr.y / static_cast<float>(renderExtent[1]);
        float prevJitterX = prev.x / static_cast<float>(renderExtent[0]);
        float prevJitterY = prev.y / static_cast<float>(renderExtent[1]);

        glm::mat4 jitteredProj = projMatrix;
        jitteredProj[2][0] += jitterX;
        jitteredProj[2][1] += jitterY;

        glm::mat4 jitteredPrevProj = prevProjMatrix;
        jitteredPrevProj[2][0] += prevJitterX;
        jitteredPrevProj[2][1] += prevJitterY;

        sceneData.jitter = {jitterX, jitterY};
        sceneData.prevJitter = {prevJitterX, prevJitterY};
        sceneData.proj = jitteredProj;
        sceneData.prevProj = jitteredPrevProj;
    }
    else {
        sceneData.jitter = {0.0f, 0.0f};
        sceneData.prevJitter = {0.0f, 0.0f};
        sceneData.proj = projMatrix;
        sceneData.prevProj = prevProjMatrix;
    }


    sceneData.viewProj = sceneData.proj * sceneData.view;
    sceneData.prevViewProj = sceneData.prevProj * sceneData.prevView;

    sceneData.invView = glm::inverse(sceneData.view);
    sceneData.invProj = glm::inverse(sceneData.proj);
    sceneData.invViewProj = glm::inverse(sceneData.viewProj);

    sceneData.clipToPrevClip = sceneData.prevProj * sceneData.prevView * sceneData.invView * sceneData.invProj;

    sceneData.cameraWorldPos = glm::vec4(view.currentViewData.cameraPos, 1.0f);

    sceneData.texelSize = glm::vec2(1.0f, 1.0f) / glm::vec2(renderExtent[0], renderExtent[1]);
    sceneData.mainRenderTargetSize = glm::vec2(renderExtent[0], renderExtent[1]);

    sceneData.depthLinearizeMult = -sceneData.proj[3][2];
    sceneData.depthLinearizeAdd = sceneData.proj[2][2];
    if (sceneData.depthLinearizeMult * sceneData.depthLinearizeAdd < 0) {
        sceneData.depthLinearizeAdd = -sceneData.depthLinearizeAdd;
    }
    float tanHalfFOVY = 1.0f / sceneData.proj[1][1];
    float tanHalfFOVX = 1.0F / sceneData.proj[0][0];
    glm::vec2 cameraTanHalfFOV{tanHalfFOVX, tanHalfFOVY};
    sceneData.ndcToViewMul = {cameraTanHalfFOV.x * 2.0f, cameraTanHalfFOV.y * -2.0f};
    sceneData.ndcToViewAdd = {cameraTanHalfFOV.x * -1.0f, cameraTanHalfFOV.y * 1.0f};
    const glm::vec2 texelSize = {1.0f / static_cast<float>(renderExtent[0]), 1.0f / static_cast<float>(renderExtent[1])};
    sceneData.ndcToViewMulXPixelSize = {sceneData.ndcToViewMul.x * texelSize.x, sceneData.ndcToViewMul.y * texelSize.y};

    sceneData.frustum = CreateFrustum(sceneData.viewProj);
    sceneData.deltaTime = deltaTime;
    float verticalFOV = view.currentViewData.fovRadians;
    sceneData.lodScreenSizeScale = (static_cast<float>(renderExtent[1]) * 0.5f) / tanf(verticalFOV * 0.5f);
    sceneData.clipPlane = {0.0f, 0.0f, 0.0f, 1.0f};

    return sceneData;
}

void SanitizeViewFamily(Core::ViewFamily& viewFamily, PipelineManager* pipelineManager, Core::Arena* arena)
{
    // Verify that shading and lighting shaders exist. If not use the default error unlit
    Core::ArenaFixedMap<StringID, bool> pipelineExists{arena, 1024};
    auto checkExists = [&](StringID id) -> bool {
        if (const bool* cached = pipelineExists.Find(id)) { return *cached; }
        bool exists = pipelineManager->GetPipelineEntry(id) != nullptr;
        pipelineExists.Insert(id, exists);
        return exists;
    };

    for (Engine::RenderMaterial& mat : viewFamily.materials) {
        bool fragOk = checkExists(mat.fragmentShader);
        bool lightOk = checkExists(mat.lightingShader);
        if (!fragOk || !lightOk) {
            mat.fragmentShader = "error_unlit"_sid;
            mat.lightingShader = "default_unlit"_sid;
        }
    }
}

void PrepareRenderFamily(Core::ViewFamily& viewFamily)
{
    // Lighting bucket finalization
    {
        uint32_t runningLightingBucketIndex{0};
        for (size_t i = 0; i < viewFamily.materials.Size(); ++i) {
            Engine::RenderMaterial& mat = viewFamily.materials[i];

            auto [lightingVal, lightingInserted] = viewFamily.lightingBuckets.TryEmplace(mat.lightingShader, runningLightingBucketIndex);
            if (lightingInserted) {
                runningLightingBucketIndex++;
            }
        }
    }

    // Text finalization
    {
        // Sort quads by (atlasBindlessIndex, textMaterialIndex) so each dispatch is wave-uniform on atlas texture.
        auto& quads = viewFamily.worldGlyphQuads;
        const auto& cpuInsts = viewFamily.textInstances;
        std::sort(quads.Data(), quads.Data() + quads.Size(), [&](const WorldGlyphQuad& a, const WorldGlyphQuad& b) {
            const Core::TextInstanceDataFull& ia = cpuInsts[a.drawCallIndex];
            const Core::TextInstanceDataFull& ib = cpuInsts[b.drawCallIndex];
            if (ia.atlasBindlessIndex != ib.atlasBindlessIndex) {
                return ia.atlasBindlessIndex < ib.atlasBindlessIndex;
            }
            return ia.textMaterialIndex < ib.textMaterialIndex;
        });

        auto& drawCalls = viewFamily.textDrawCalls;
        if (!quads.IsEmpty()) {
            uint32_t runStart = 0;
            const Core::TextInstanceDataFull* prev = &cpuInsts[quads[0].drawCallIndex];
            for (uint32_t i = 1; i < quads.Size(); ++i) {
                const Core::TextInstanceDataFull& cur = cpuInsts[quads[i].drawCallIndex];
                if (cur.atlasBindlessIndex != prev->atlasBindlessIndex || cur.textMaterialIndex != prev->textMaterialIndex) {
                    drawCalls.PushBack({runStart, i - runStart, prev->atlasBindlessIndex, prev->textMaterialIndex});
                    runStart = i;
                    prev = &cur;
                }
            }
            drawCalls.PushBack({runStart, static_cast<uint32_t>(quads.Size()) - runStart, prev->atlasBindlessIndex, prev->textMaterialIndex});
        }
    }
}


RenderFamilyProperties PrepareRenderFamilyProperties(Core::ViewFamily& viewFamily, ReadbackStruct* readbackData, PipelineManager* _pipelineManager, FrameResourceLimits& _limits)
{
    RenderFamilyProperties renderFamilyProperties{};
    renderFamilyProperties.viewFamily = &viewFamily;
    renderFamilyProperties.bCanRender = _pipelineManager->IsCategoryReady(PipelineCategory::Critical);

    if (!viewFamily.instances.IsEmpty()) {
        std::ranges::sort(viewFamily.instances, [](const Core::InstanceData& a, const Core::InstanceData& b) {
            return a.primitiveIndex < b.primitiveIndex;
        });
    }


    _limits.highestModelCount = std::max(_limits.highestModelCount, NextPowerOfTwo(viewFamily.modelMatrices.Size()));
    _limits.highestMaterialCount = std::max(_limits.highestMaterialCount, NextPowerOfTwo(viewFamily.materials.Size()));
    _limits.highestLightingCount = std::max(_limits.highestLightingCount, NextPowerOfTwo(viewFamily.lightingBuckets.Size()));

    uint32_t totalInstanceCountThisFrame = viewFamily.instances.Size();
    _limits.highestInstanceCount = std::max(_limits.highestInstanceCount, NextPowerOfTwo(totalInstanceCountThisFrame));
    _limits.highestMeshletCount = std::max(_limits.highestMeshletCount, NextPowerOfTwo(readbackData->meshletCount));
    _limits.highestGlyphQuadCount = std::max(_limits.highestGlyphQuadCount, NextPowerOfTwo(viewFamily.worldGlyphQuads.Size()));
    _limits.highestUIGlyphQuadCount = std::max(_limits.highestUIGlyphQuadCount, NextPowerOfTwo(viewFamily.uiGlyphQuads.Size()));
    _limits.highestTextInstanceCount = std::max(_limits.highestTextInstanceCount, NextPowerOfTwo(viewFamily.textInstances.Size()));
    _limits.highestTextMaterialCount = std::max(_limits.highestTextMaterialCount, NextPowerOfTwo(viewFamily.textMaterials.Size()));


    renderFamilyProperties.modelBufferSize = _limits.highestModelCount * sizeof(Model);
    renderFamilyProperties.materialBufferSize = _limits.highestMaterialCount * sizeof(MaterialProperties);
    renderFamilyProperties.shadeDispatchBufferSize = _limits.highestMaterialCount * sizeof(ShadeDispatchParameters);
    renderFamilyProperties.lightingDispatchBufferSize = _limits.highestLightingCount * sizeof(LightingDispatchParameters);
    renderFamilyProperties.instanceBufferSize = _limits.highestInstanceCount * sizeof(Instance);


    renderFamilyProperties.instanceMeshletOffsetsBufferSize = _limits.highestInstanceCount * sizeof(InstanceMeshletOffsetPrefixSum);
    uint32_t level1BlockCount = (_limits.highestInstanceCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;
    uint32_t level2BlockCount = (level1BlockCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;
    renderFamilyProperties.level1SumsBufferSize = _limits.highestInstanceCount * sizeof(uint32_t);
    renderFamilyProperties.level1BlockSumsBufferSize = level1BlockCount * sizeof(uint32_t);
    renderFamilyProperties.level2SumsBufferSize = level1BlockCount * sizeof(uint32_t);
    renderFamilyProperties.level2BlockSumsBufferSize = level2BlockCount * sizeof(uint32_t);
    renderFamilyProperties.scannedLevel2BlockSumsBufferSize = glm::max(level2BlockCount, INSTANCING_PREFIX_SUM_DISPATCH_X) * sizeof(uint32_t);

    renderFamilyProperties.intermediateMeshletBufferSize = _limits.highestMeshletCount * sizeof(IntermediateMeshlet);
    uint32_t meshletLevel1BlockCount = (_limits.highestMeshletCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;
    uint32_t meshletLevel2BlockCount = (meshletLevel1BlockCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;

    renderFamilyProperties.meshletLevel1SumsBufferSize = _limits.highestMeshletCount * sizeof(uint32_t);
    renderFamilyProperties.meshletLevel1BlockSumsBufferSize = meshletLevel1BlockCount * sizeof(uint32_t);
    renderFamilyProperties.meshletLevel2SumsBufferSize = meshletLevel1BlockCount * sizeof(uint32_t);
    renderFamilyProperties.meshletLevel2BlockSumsBufferSize = meshletLevel2BlockCount * sizeof(uint32_t);
    renderFamilyProperties.meshletScannedLevel2BlockSumsBufferSize = glm::max(meshletLevel2BlockCount, INSTANCING_PREFIX_SUM_DISPATCH_X) * sizeof(uint32_t);

    renderFamilyProperties.visibleMeshletsBufferSize = _limits.highestMeshletCount * sizeof(CompactedMeshlet);
    renderFamilyProperties.visibleMeshletUpperBound = _limits.highestMeshletCount;

    renderFamilyProperties.glyphQuadBufferSize = _limits.highestGlyphQuadCount * sizeof(WorldGlyphQuad);
    renderFamilyProperties.uiGlyphQuadBufferSize = _limits.highestUIGlyphQuadCount * sizeof(UIGlyphQuad);
    renderFamilyProperties.textInstanceBufferSize = _limits.highestTextInstanceCount * sizeof(TextInstanceData);
    renderFamilyProperties.textMaterialBufferSize = _limits.highestTextMaterialCount * sizeof(TextRenderMaterial);

    return renderFamilyProperties;
}

float Halton(uint32_t i, uint32_t b)
{
    float f = 1.0f;
    float r = 0.0f;

    while (i > 0) {
        f /= static_cast<float>(b);
        r = r + f * static_cast<float>(i % b);
        i = static_cast<uint32_t>(floorf(static_cast<float>(i) / static_cast<float>(b)));
    }

    return r;
}
} // Render
