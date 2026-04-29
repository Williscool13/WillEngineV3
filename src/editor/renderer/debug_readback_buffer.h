//
// Created by William on 2026-04-29.
//

#ifndef WILL_ENGINE_DEBUG_READBACK_BUFFER_H
#define WILL_ENGINE_DEBUG_READBACK_BUFFER_H

#include "core/containers/inline_function.h"
#include "core/containers/inline_vector.h"
#include "render/render-graph/render_graph_resources.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
struct RenderGraph;
struct VulkanContext;
}

namespace Editor
{
/**
 * Debug-only readback helper. Not synchronized; reads lag by several frames.
 * Register all entries once at startup via Register(), then each frame:
 */
struct DebugReadbackBuffer
{
    /**
     * @param context
     * @param bufferSize Total byte capacity; must be large enough to hold all registered entries.
     */
    void Init(Render::VulkanContext* context, size_t bufferSize);

    /**
     * Call once at startup. Allocates sizeof(T) bytes in the buffer for this entry.
     * @param label Display name shown in ImGui and used as the RDG pass name.
     * @param copyFn Adds RDG copy passes writing into dstBuffer at dstOffset.
     * @param presentFn Receives typed data and writes ImGui widgets.
     */
    template<typename T>
    void Register(
        const char* label,
        Core::InlineFunction<void(Render::RenderGraph&, StringID, size_t)> copyFn,
        Core::InlineFunction<void(const T&)> presentFn
    )
    {
        const size_t offset = usedSize;
        usedSize += sizeof(T);

        entries.PushBack(Entry{
            label,
            offset,
            sizeof(T),
            std::move(copyFn),
            [presentFn = std::move(presentFn)](const void* data) {
                presentFn(*static_cast<const T*>(data));
            }
        });
    }

    /**
     * Render thread. Calls each registered copyFn to add RDG copy passes for this frame.
     * @param graph
     * @param name The imported debug readback buffer name in the RDG.
     */
    void ScheduleCopies(Render::RenderGraph& graph, StringID name);

    /**
     * Engine thread. Renders an ImGui collapser per registered entry.
     */
    void Present();

    VkBuffer GetHandle() const { return buffer.handle; }
    VkDeviceAddress GetAddress() const { return buffer.address; }
    size_t GetSize() const { return buffer.allocationInfo.size; }
    Render::PipelineEvent GetLastKnownState() const { return lastKnownState; }
    void SetLastKnownState(Render::PipelineEvent state) { lastKnownState = state; }

private:
    struct Entry
    {
        const char* label;
        size_t offset;
        size_t size;
        Core::InlineFunction<void(Render::RenderGraph&, StringID, size_t)> copyFn;
        Core::InlineFunction<void(const void*), 128> presentFn;
    };

    Render::AllocatedBuffer buffer;
    Render::PipelineEvent lastKnownState;
    size_t usedSize{0};
    Core::InlineVector<Entry, 128> entries;
};
} // Editor

#endif //WILL_ENGINE_DEBUG_READBACK_BUFFER_H
