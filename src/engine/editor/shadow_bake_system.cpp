//
// Created by William on 2026-10-09.
//

#include "shadow_bake_system.h"

#include <cstring>
#include <glm/glm.hpp>

#include "engine/engine_api.h"
#include "engine/asset_manager.h"
#include "engine/include/engine_context.h"
#include "engine/logging/engine_log.h"
#include "engine/components/render/light_components.h"
#include "engine/resources/wimage_format.h"
#include "engine/systems/render_systems.h"
#include "core/containers/inline_string.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "render/render-view/local_shadow_views.h"

namespace Engine
{
ShadowBakeKey MakeShadowBakeKey(const LightInfo& light)
{
    const Render::LocalShadowBakeParams params = Render::GetLocalShadowBakeParams(light);
    ShadowBakeKey key{};
    for (int32_t i = 0; i < 3; ++i) {
        key.eye[i] = params.eye[i];
        key.forward[i] = params.forward[i];
    }
    key.tanHalf = params.tanHalf;
    key.nearPlane = params.nearPlane;
    key.farPlane = params.farPlane;
    key.faceCount = params.faceCount;
    return key;
}

bool IsShadowBakeCurrent(const ShadowBakeKey& baked, const LightInfo& light)
{
    constexpr float EPSILON = 1e-3f;
    const ShadowBakeKey live = MakeShadowBakeKey(light);
    bool bMatch = baked.faceCount == live.faceCount
                  && glm::abs(baked.tanHalf - live.tanHalf) <= EPSILON
                  && glm::abs(baked.nearPlane - live.nearPlane) <= EPSILON
                  && glm::abs(baked.farPlane - live.farPlane) <= EPSILON;
    for (int32_t i = 0; i < 3 && bMatch; ++i) {
        bMatch = glm::abs(baked.eye[i] - live.eye[i]) <= EPSILON && glm::abs(baked.forward[i] - live.forward[i]) <= EPSILON;
    }
    return bMatch;
}

#if WILL_EDITOR

static bool LightForBake(EngineState* state, entt::entity entity, uint64_t& outShadowId, uint32_t& outResolution, LightInfo& outLight)
{
    entt::registry& registry = state->registry;
    if (!registry.valid(entity)) { return false; }

    uint32_t slot = AnalyticLightStore::INVALID_SLOT;
    if (const auto* area = registry.try_get<Component::AreaLightComponent>(entity); area && area->shadowMode == Component::LightShadowMode::Baked) {
        slot = area->lightSlot;
        outShadowId = area->shadowId;
        outResolution = Component::ShadowBakeResolutionPixels(area->shadowBakeResolution);
    }
    else if (const auto* sphere = registry.try_get<Component::SphereLightComponent>(entity); sphere && sphere->shadowMode == Component::LightShadowMode::Baked) {
        slot = sphere->lightSlot;
        outShadowId = sphere->shadowId;
        outResolution = Component::ShadowBakeResolutionPixels(sphere->shadowBakeResolution);
    }
    if (slot == AnalyticLightStore::INVALID_SLOT || outShadowId == 0) { return false; }

    outLight = state->analyticLightStore.Lights()[slot];
    return outLight.range > 0.0f;
}

static void WriteShadowMap(EngineContext* ctx, uint64_t shadowId, uint32_t resolution, const ShadowBakeKey& key, const Core::HeapArray<uint16_t>& pixels)
{
    const WImageDesc desc{VK_FORMAT_R16_UNORM, resolution, resolution, 1, key.faceCount};
    const size_t blobSize = WImageBlobSize(desc);
    auto blob = Core::HeapArray<uint8_t>(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::EngineContext, blobSize);
    WImageBlobInit(blob.Data(), blob.Size(), desc);
    const size_t faceBytes = static_cast<size_t>(resolution) * resolution * sizeof(uint16_t);
    for (uint32_t face = 0; face < key.faceCount; ++face) {
        memcpy(WImageFaceData(blob.Data(), 0, face), reinterpret_cast<const uint8_t*>(pixels.Data()) + face * faceBytes, faceBytes);
    }

    auto compressed = Core::HeapArray<uint8_t>(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::EngineContext, CompressMaxSize(CompressionType::Zstd, blobSize));
    const size_t compressedSize = Compress(CompressionType::Zstd, blob.Data(), blobSize, compressed.Data(), compressed.Size());

    const AssetManager::ShadowMapInfo* previous = ctx->assetManager->GetShadowMapInfo(shadowId);
    WShadowMapHeader header{};
    header.shadowId = shadowId;
    header.contentVersion = previous ? previous->contentVersion + 1 : 1;
    header.resolution = resolution;
    header.key = key;
    header.dataSize = compressedSize;
    header.uncompressedSize = blobSize;

    const Core::Path directory = Platform::GetAssetPath() / "shadows";
    Platform::CreateDirectories(directory);
    const Core::InlineString<64> fileName = Core::InlineString<64>::Format("shadow_%llu.wshadowmap", static_cast<unsigned long long>(shadowId));
    const Core::Path path = directory / fileName.c_str();

    Core::Vector<std::byte> headerOut(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::EngineContext);
    WriteWShadowMapHeader(headerOut, header);
    if (!Platform::WriteFile(path, headerOut.Data(), headerOut.Size()) || !Platform::AppendFile(path, compressed.Data(), compressedSize)) {
        LOG_ERROR(Asset, "Failed to write {}", path.c_str());
        return;
    }
    ctx->rescan.bResources = true;
    LOG_INFO(Asset, "Baked shadow map {}", path.c_str());
}

void ShadowBakeSystem::Enqueue(entt::entity light)
{
    if (light == entity || queue.Contains(light) || queue.IsFull()) { return; }
    if (!IsBusy()) {
        batchTotal = 0;
        batchDone = 0;
    }
    queue.PushBack(light);
    ++batchTotal;
}

void ShadowBakeSystem::EnqueueAll(entt::registry& registry)
{
    for (const auto [light, comp] : registry.view<Component::AreaLightComponent>().each()) {
        if (comp.shadowMode == Component::LightShadowMode::Baked) { Enqueue(light); }
    }
    for (const auto [light, comp] : registry.view<Component::SphereLightComponent>().each()) {
        if (comp.shadowMode == Component::LightShadowMode::Baked) { Enqueue(light); }
    }
}

void ShadowBakeSystem::Cancel()
{
    queue.Clear();
    requestId = 0;
    entity = entt::null;
    pixels = {};
}

void ShadowBakeSystem::Tick(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->shadowBake = {};
    ShadowBakeCaptureStaging& capture = ctx->shadowBakeCapture;

    if (capture.bReady.load(std::memory_order_acquire)) {
        if (requestId != 0 && capture.requestId == requestId && capture.resolution == resolution) {
            const size_t faceTexels = static_cast<size_t>(resolution) * resolution;
            memcpy(pixels.Data() + face * faceTexels, capture.pixels.Data(), faceTexels * sizeof(uint16_t));
            ++face;
            if (face == key.faceCount) {
                WriteShadowMap(ctx, shadowId, resolution, key, pixels);
                ++batchDone;
                requestId = 0;
                entity = entt::null;
                pixels = {};
            }
            else {
                requestId = nextRequestId++;
            }
        }
        capture.pixels = {};
        capture.bReady.store(false, std::memory_order_release);
    }

    while (requestId == 0 && !queue.IsEmpty()) {
        const entt::entity next = queue.PopBackValue();
        if (!LightForBake(state, next, shadowId, resolution, light)) {
            ++batchDone;
            continue;
        }
        entity = next;
        key = MakeShadowBakeKey(light);
        face = 0;
        pixels = Core::HeapArray<uint16_t>(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::EngineContext, static_cast<size_t>(resolution) * resolution * key.faceCount);
        // Physics bodies may have changed since the flags were last evaluated
        EvaluateAllInstanceRenderStates(state);
        requestId = nextRequestId++;
    }

    if (requestId != 0) {
        frameBuffer->shadowBake = {
            .requestId = requestId,
            .light = light,
            .face = face,
            .resolution = resolution,
            .slopeBias = state->lighting.localShadows.slopeBias,
        };
    }
}

void ShadowBakeTick(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer)
{
    state->shadowBake.Tick(ctx, state, frameBuffer);
}

#else

void ShadowBakeSystem::Enqueue(entt::entity) {}
void ShadowBakeSystem::EnqueueAll(entt::registry&) {}
void ShadowBakeSystem::Cancel() {}
void ShadowBakeSystem::Tick(EngineContext*, EngineState*, Core::FrameBuffer*) {}
void ShadowBakeTick(EngineContext*, EngineState*, Core::FrameBuffer*) {}

#endif
} // Engine
