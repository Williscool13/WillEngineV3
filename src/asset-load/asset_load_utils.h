//
// Created by William on 2026-04-06.
//

#ifndef WILL_ENGINE_ASSET_LOAD_UTILS_H
#define WILL_ENGINE_ASSET_LOAD_UTILS_H
#include "core/containers/span.h"
#include "engine/resources/model/static_model.h"

namespace AssetLoad {
Engine::ModelBounds ComputeBounds(Core::Span<Vec3> positions, Core::Span<uint32_t> indices = {});
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_UTILS_H
