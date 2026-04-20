//
// Created by William on 2026-04-06.
//

#ifndef WILL_ENGINE_ASSET_LOAD_UTILS_H
#define WILL_ENGINE_ASSET_LOAD_UTILS_H
#include "core/containers/span.h"
#include "engine/resources/model/static_model.h"

namespace AssetLoad {
Mat3 JacobiEigen3x3(const Mat3& symMat);

Engine::MeshBounds CalculateMeshBounds(Core::Span<Engine::FullVertex> vertices);
Engine::ModelBounds ComputeBounds(Core::Span<Vec3> positions);
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_UTILS_H
