//
// Created by William on 2026-06-14.
//

#include "builtin_assets.h"

#include "asset_manager.h"
#include "resources/model/model_types.h"

namespace Engine
{
static StaticModelHandle GetOrLoad(IAssetLoader* assetLoader, StaticModelHandle& cache, ProceduralParams params)
{
    if (!cache.IsValid()) {
        cache = assetLoader->LoadProceduralModel(params);
    }
    return cache;
}

StaticModelHandle BuiltinAssets::GetUnitQuad(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitQuad, PlaneParams{.sizeX = 1.0f, .sizeZ = 1.0f, .tilesX = 1, .tilesZ = 1}); }
StaticModelHandle BuiltinAssets::GetUnitDisk(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitDisk, RingParams{.outerRadius = 0.5f, .innerRadius = 0.0f, .slices = 128, .bDoubleSided = false}); }
StaticModelHandle BuiltinAssets::GetUnitCube(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitCube, BoxParams{.sizeX = 1.0f, .sizeY = 1.0f, .sizeZ = 1.0f}); }
StaticModelHandle BuiltinAssets::GetUnitSphere(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitSphere, SphereParams{.radius = 0.5f, .slices = 32, .stacks = 16}); }
StaticModelHandle BuiltinAssets::GetUnitCylinder(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitCylinder, CylinderParams{.radius = 0.5f, .height = 1.0f, .slices = 32, .bCapped = true}); }
StaticModelHandle BuiltinAssets::GetUnitCone(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitCone, ConeParams{.radius = 0.5f, .height = 1.0f, .slices = 32, .bCapped = true}); }
StaticModelHandle BuiltinAssets::GetUnitCapsule(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitCapsule, CapsuleParams{.radius = 0.25f, .height = 0.5f, .slices = 32, .rings = 8}); }
StaticModelHandle BuiltinAssets::GetUnitTorus(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitTorus, TorusParams{.ringRadius = 0.375f, .tubeRadius = 0.125f, .slices = 32, .stacks = 16}); }
StaticModelHandle BuiltinAssets::GetUnitHemisphere(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitHemisphere, HemisphereParams{.radius = 0.5f, .slices = 32, .stacks = 16}); }
StaticModelHandle BuiltinAssets::GetUnitTetrahedron(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitTetrahedron, TetrahedronParams{.radius = 0.5f}); }
StaticModelHandle BuiltinAssets::GetUnitOctahedron(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitOctahedron, OctahedronParams{.radius = 0.5f}); }
StaticModelHandle BuiltinAssets::GetUnitIcosahedron(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitIcosahedron, IcosahedronParams{.radius = 0.5f}); }
StaticModelHandle BuiltinAssets::GetUnitDodecahedron(IAssetLoader* assetLoader) { return GetOrLoad(assetLoader, unitDodecahedron, DodecahedronParams{.radius = 0.5f}); }
} // Engine
