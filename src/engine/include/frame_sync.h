//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_FRAME_SYNC_H
#define WILL_ENGINE_FRAME_SYNC_H

#include <mutex>

#include <imgui/imgui_threaded_rendering.h>

#include "core/containers/array.h"
#include "core/containers/inline_queue.h"
#include "render/interface/render_interface.h"

namespace Core
{
struct FrameSync
{
    FrameSync() = default;

    explicit FrameSync(MemoryManager& memoryManager);

    ~FrameSync() = default;

    Array<FrameBuffer, FRAME_BUFFER_COUNT + 1> frameBuffers{};
    Array<ImDrawDataSnapshot, FRAME_BUFFER_COUNT + 1> imguiDataSnapshots{};
public: // Engine Thread
    /**
     * Gets the frame buffer from 3 FIFs ago, used to collect readbacks.
     * Called from Engine. Do NOT call in render thread.
     * @return
     */
    FrameBuffer* GetFrameBufferMinusFIF() { return &frameBuffers[(currentFrameBufferIndex + 1) % frameBuffers.Size()]; }
    /**
     * Called from Engine. Do NOT call in render thread.
     * @return
     */
    FrameBuffer* GetCurrentFrameBuffer() { return &frameBuffers[currentFrameBufferIndex]; }
    ImDrawDataSnapshot* GetCurrentImguiSnapshot() { return &imguiDataSnapshots[currentFrameBufferIndex]; }
    void NextFrameBuffer() { currentFrameBufferIndex = (currentFrameBufferIndex + 1) % frameBuffers.Size(); }

    uint32_t currentFrameBufferIndex{};


public: // Render Thread
    void NextRenderFrame() { currentRenderFrame = (currentRenderFrame + 1) % renderFrameBuffer.Size(); }

    uint32_t currentRenderFrame{};
    /**
     * The frameBuffer index to use for each render FIF
     */
    Array<uint32_t, FRAME_BUFFER_COUNT> renderFrameBuffer{};


    void SignalRenderFrame()
    {
        {
            std::lock_guard lock(renderMutex);
            renderFrames.fetch_add(1);
        }
        renderCV.notify_one();
    }

    std::atomic<uint32_t> gameFrames{3};
    std::mutex renderMutex;
    std::condition_variable renderCV;
    std::atomic<uint32_t> renderFrames{0};
};
} // Core

#endif //WILL_ENGINE_FRAME_SYNC_H
