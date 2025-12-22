//
// Created by William on 2025-12-22.
//

#include "asset_manager.h"

namespace Engine
{
AssetManager::AssetManager(Render::ResourceManager* resourceManager)
    : resourceManager(resourceManager)
{}

AssetManager::~AssetManager() {}

WillModelHandle AssetManager::LoadModel(const std::filesystem::path& path)
{
    return WillModelHandle::INVALID;
}

Render::WillModel* AssetManager::GetModel(WillModelHandle handle)
{
    return nullptr;
}

void AssetManager::UnloadModel(WillModelHandle handle) {}
} // Engine
