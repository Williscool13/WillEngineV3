//
// Created by William on 2026-09-05.
//

#include "core/memory/tlsf_allocator.h"

namespace Core
{
const char* AllocTagName(AllocTag tag)
{
    switch (tag) {
        case AllocTag::Unknown: return "Unknown";
        case AllocTag::AssetModel: return "AssetModel";
        case AllocTag::AssetTexture: return "AssetTexture";
        case AllocTag::AssetGenerator: return "AssetGenerator";
        case AllocTag::Physics: return "Physics";
        case AllocTag::RenderMesh: return "RenderMesh";
        case AllocTag::RenderMaterial: return "RenderMaterial";
        case AllocTag::Render: return "Render";
        case AllocTag::ECS: return "ECS";
        case AllocTag::TaskScheduler: return "TaskScheduler";
        case AllocTag::SDL: return "SDL";
        case AllocTag::ImGui: return "ImGui";
        case AllocTag::Editor: return "Editor";
        case AllocTag::EngineLogger: return "EngineLogger";
        case AllocTag::EngineContext: return "EngineContext";
        case AllocTag::EngineState: return "EngineState";
        case AllocTag::GameState: return "GameState";
        case AllocTag::InputManager: return "InputManager";
        case AllocTag::TimeManager: return "TimeManager";
        case AllocTag::FrameSync: return "FrameSync";
        case AllocTag::FrameSync0: return "FrameSync0";
        case AllocTag::FrameSync1: return "FrameSync1";
        case AllocTag::FrameSync2: return "FrameSync2";
        case AllocTag::FrameSync3: return "FrameSync3";
        case AllocTag::RenderThread: return "RenderThread";
        case AllocTag::AudioManager: return "AudioManager";
        case AllocTag::AsyncAssetLoadManager: return "AsyncAssetLoadManager";
        case AllocTag::AssetManager: return "AssetManager";
        case AllocTag::MaterialManager: return "MaterialManager";
        case AllocTag::Clay: return "Clay";
        case AllocTag::Meshopt: return "Meshopt";
        case AllocTag::Vulkan: return "Vulkan";
        case AllocTag::ParShapes: return "ParShapes";
        case AllocTag::Earcut: return "Earcut";
        case AllocTag::Queue: return "Queue";
        case AllocTag::Stbi: return "Stbi";
        case AllocTag::Bc7enc: return "Bc7enc";
        case AllocTag::HarfBuzz: return "HarfBuzz";
        case AllocTag::MCPServer: return "MCPServer";
        case AllocTag::Count: return "Count";
    }
    return "Unknown";
}
} // Core
