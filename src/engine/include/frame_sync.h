//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_FRAME_SYNC_H
#define WILL_ENGINE_FRAME_SYNC_H
#include <array>
#include <mutex>

#include <imgui/imgui_threaded_rendering.h>

#include "core/containers/array.h"
#include "render/interface/render_interface.h"

namespace Core
{
struct FrameSync
{
    Array<FrameBuffer, FRAME_BUFFER_COUNT> frameBuffers{};
    Array<ImDrawDataSnapshot, FRAME_BUFFER_COUNT> imguiDataSnapshots{};

    std::atomic<uint32_t> gameFrames{3};
    std::mutex renderMutex;
    std::condition_variable renderCV;
    std::atomic<uint32_t> renderFrames{0};
};
} // Core

#endif //WILL_ENGINE_FRAME_SYNC_H