//
// Created by William on 2026-10-25.
//

#include "runtime_save.h"

#include <tracy/Tracy.hpp>

#include "core/containers/inline_vector.h"
#include "engine/engine_api.h"
#include "engine/editor/mobility_assign.h"
#include "engine/include/engine_context.h"
#include "engine/logging/engine_log.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "engine/systems/scene_system.h"
#include "platform/file_utils.h"
#include "platform/paths.h"

namespace Engine
{
static constexpr uint32_t RUNTIME_SAVE_VERSION = 1;

static Core::Path RuntimeSavePath(const char* name)
{
    return Platform::GetUserDataPath() / "saves" / Core::InlineString<128>::Format("%s.wsave", name).c_str();
}

bool SaveRuntime(EngineContext* ctx, EngineState* state, const char* name)
{
    ZoneScoped;
    if (!IsRunning(state)) {
        LOG_WARN(Engine, "Runtime save refused: only while playing or simulating");
        return false;
    }

    Core::Vector<std::byte> out(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    TextWriter w(out);
    w.Key("wsave", RUNTIME_SAVE_VERSION);
    w.Key("runSeconds", state->timeFrame->totalTime - state->runStartTime);
    for (int i = 0; i < 4; ++i) {
        w.Key(Core::InlineString<8>::Format("rng%d", i).c_str(), state->rng.GetState(i));
    }
    w.Count("scenes", static_cast<uint32_t>(state->editor.loadedScenes.Size()));
    for (const RuntimeSceneMetadata& meta : state->editor.loadedScenes) {
        const Scene scene = SaveScene(state->componentRegistry, state->registry, ctx->assetManager, meta.sceneId, true);
        w.BeginBlock("scene");
        w.Raw(scene.content.Data(), scene.content.Size());
        w.EndBlock();
    }

    const Core::Path path = RuntimeSavePath(name);
    Platform::CreateDirectories(path.Parent());
    if (!Platform::WriteFile(path, out.Data(), out.Size())) {
        LOG_ERROR(Engine, "Failed to write runtime save '{}'", path.c_str());
        return false;
    }
    LOG_INFO(Engine, "Saved runtime state to '{}'", path.c_str());
    return true;
}

bool LoadRuntime(EngineContext* ctx, EngineState* state, const char* name)
{
    ZoneScoped;
    if (!IsRunning(state)) {
        LOG_WARN(Engine, "Runtime load refused: only while playing or simulating");
        return false;
    }

    const Core::Path path = RuntimeSavePath(name);
    Platform::ScopedFileMapping map(path);
    if (!map.data) {
        LOG_ERROR(Engine, "Failed to open runtime save '{}'", path.c_str());
        return false;
    }
    const TextReader reader(map.data, map.size);
    if (reader.UInt("wsave") != RUNTIME_SAVE_VERSION) {
        LOG_ERROR(Engine, "Runtime save '{}' is an unknown version", path.c_str());
        return false;
    }

    Core::InlineVector<StringID, 8> scenes;
    for (const RuntimeSceneMetadata& meta : state->editor.loadedScenes) {
        scenes.PushBack(meta.sceneId);
    }
    bool bSameScenes = reader.RecordCount("scenes") == scenes.Size();
    reader.ForEachRecord("scenes", [&](const TextReader& sceneReader) { bSameScenes &= scenes.Contains(StringID{sceneReader.U64("scene_id")}); });
    if (!bSameScenes) {
        LOG_WARN(Engine, "Runtime load refused: '{}' was saved with other scenes loaded", path.c_str());
        return false;
    }

    const bool bPlaying = IsPlaying(state);
    if (bPlaying && ctx->playStopFn) {
        ctx->playStopFn(ctx, state);
    }

    UnloadScenes(state, scenes);
    reader.ForEachRecord("scenes", [&](const TextReader& sceneReader) {
        state->editor.loadedScenes.PushBack({LoadScene(state->componentRegistry, state->registry, sceneReader)});
    });
    ResolvePrefabLoads(state, ctx->assetManager);
    ResolveHierarchyLinks(state);
    LockStaticTransforms(state);

    for (int i = 0; i < 4; ++i) {
        state->rng.SetState(i, reader.U64(Core::InlineString<8>::Format("rng%d", i).c_str()));
    }
    state->runStartTime = state->timeFrame->totalTime - reader.Float("runSeconds");
    state->requests.RequestRenderReset(Core::RenderReset::Cut);

    if (bPlaying && ctx->playStartFn) {
        ctx->playStartFn(ctx, state);
    }
    LOG_INFO(Engine, "Loaded runtime state from '{}'", path.c_str());
    return true;
}
} // Engine
