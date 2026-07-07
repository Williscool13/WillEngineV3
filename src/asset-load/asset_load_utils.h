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

/**
 * Extracts local-space triangles of emissive primitives into outputModel->modelData.emissiveTriangles for ReSTIR light gathering.
 * Verts are stored (v0, e1 = v1-v0, e2 = v2-v0) in BLAS index order so a RayQuery PrimitiveIndex() equals the triangle index.
 * MUST be called while rawData is fully local: primitive.indexOffset and rawData.indices not yet rebased to global, and prop.index still local.
 *
 * @param primitiveOffsetCount Global primitive-buffer base, added to each local prop.index to form the set key (matches MeshPrimitiveInstance.primitiveIndex).
 * @param bForceAllEmissive When true (procedural meshes carry no source material), extracts every primitive regardless of material; when false, only primitives whose default material is emissive.
 */
void ExtractEmissiveTriangles(const UnpackedStaticModel& rawData, Engine::StaticModel* outputModel, Core::MemoryManager* memoryManager, uint32_t primitiveOffsetCount, bool bForceAllEmissive);
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_UTILS_H
