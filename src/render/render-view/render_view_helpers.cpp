//
// Created by William on 2026-01-21.
//

#include "render_view_helpers.h"

#include "core/include/render_interface.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "render/types/render_types.h"

namespace Render
{
SceneData GenerateSceneData(const Core::RenderView& view, const Core::PostProcessConfiguration& ppConfig, std::array<uint32_t, 2> renderExtent, uint64_t frameNumber, float deltaTime)
{
    const glm::mat4 viewMatrix = glm::lookAt(view.currentViewData.cameraPos, view.currentViewData.cameraLookAt, view.currentViewData.cameraUp);
    const glm::mat4 projMatrix = glm::perspective(view.currentViewData.fovRadians, view.currentViewData.aspectRatio, view.currentViewData.farPlane, view.currentViewData.nearPlane);

    const glm::mat4 prevViewMatrix = glm::lookAt(view.previousViewData.cameraPos, view.previousViewData.cameraLookAt, view.previousViewData.cameraUp);
    const glm::mat4 prevProjMatrix = glm::perspective(view.previousViewData.fovRadians, view.previousViewData.aspectRatio, view.previousViewData.farPlane, view.previousViewData.nearPlane);

    SceneData sceneData{};
    sceneData.view = viewMatrix;
    sceneData.prevView = prevViewMatrix;

    if (ppConfig.bEnableTemporalAntialiasing) {
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


    sceneData.unjitteredViewProj = projMatrix * viewMatrix;
    sceneData.unjitteredPrevViewProj = prevProjMatrix * prevViewMatrix;
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

    return sceneData;
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
