//
// Created by William on 2026-04-06.
//

#ifndef WILL_ENGINE_ASSET_LOAD_UTILS_H
#define WILL_ENGINE_ASSET_LOAD_UTILS_H
#include "core/containers/span.h"
#include "engine/resources/model/static_model.h"

namespace Core { class MemoryManager; }

namespace AssetLoad {
struct UnpackedStaticModel;

Mat3 JacobiEigen3x3(const Mat3& symMat);

Engine::MeshBounds CalculateMeshBounds(Core::Span<Engine::FullVertex> vertices);
Engine::ModelBounds ComputeBounds(Core::Span<Vec3> positions);

/** Decodes a compressed vertex's unorm16 quantized position back to model space (bit-exact match of the historical inline expansions). */
inline Vec3 DequantizeVertexPosition(const Engine::Vertex& v, const Vec3& boundsMin, const Vec3& boundsExtents)
{
    return Vec3{
        static_cast<float>(v.pos0 & 0xFFFF) / 65535.0f * boundsExtents.x + boundsMin.x,
        static_cast<float>(v.pos0 >> 16 & 0xFFFF) / 65535.0f * boundsExtents.y + boundsMin.y,
        static_cast<float>(v.pos1 & 0xFFFF) / 65535.0f * boundsExtents.z + boundsMin.z
    };
}

} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_UTILS_H
