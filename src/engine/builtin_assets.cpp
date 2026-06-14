//
// Created by William on 2026-06-14.
//

#include "builtin_assets.h"

#include "asset_manager.h"
#include "resources/model/model_types.h"

namespace Engine
{
static StaticModelHandle GetOrLoad(AssetManager* assetManager, StaticModelHandle& cache, ProceduralParams params)
{
    if (!cache.IsValid()) {
        cache = assetManager->LoadProceduralModel(params);
    }
    return cache;
}

StaticModelHandle BuiltinAssets::GetUnitQuad(AssetManager* assetManager) { return GetOrLoad(assetManager, unitQuad, PlaneParams{.sizeX = 1.0f, .sizeZ = 1.0f, .tilesX = 1, .tilesZ = 1}); }
StaticModelHandle BuiltinAssets::GetUnitCube(AssetManager* assetManager) { return GetOrLoad(assetManager, unitCube, BoxParams{.sizeX = 1.0f, .sizeY = 1.0f, .sizeZ = 1.0f}); }
StaticModelHandle BuiltinAssets::GetUnitSphere(AssetManager* assetManager) { return GetOrLoad(assetManager, unitSphere, SphereParams{.radius = 0.5f, .slices = 32, .stacks = 16}); }
StaticModelHandle BuiltinAssets::GetUnitCylinder(AssetManager* assetManager) { return GetOrLoad(assetManager, unitCylinder, CylinderParams{.radius = 0.5f, .height = 1.0f, .slices = 32, .bCapped = true}); }
StaticModelHandle BuiltinAssets::GetUnitCone(AssetManager* assetManager) { return GetOrLoad(assetManager, unitCone, ConeParams{.radius = 0.5f, .height = 1.0f, .slices = 32, .bCapped = true}); }
StaticModelHandle BuiltinAssets::GetUnitCapsule(AssetManager* assetManager) { return GetOrLoad(assetManager, unitCapsule, CapsuleParams{.radius = 0.25f, .height = 0.5f, .slices = 32, .rings = 8}); }
StaticModelHandle BuiltinAssets::GetUnitTorus(AssetManager* assetManager) { return GetOrLoad(assetManager, unitTorus, TorusParams{.ringRadius = 0.375f, .tubeRadius = 0.125f, .slices = 32, .stacks = 16}); }
StaticModelHandle BuiltinAssets::GetUnitHemisphere(AssetManager* assetManager) { return GetOrLoad(assetManager, unitHemisphere, HemisphereParams{.radius = 0.5f, .slices = 32, .stacks = 16}); }
StaticModelHandle BuiltinAssets::GetUnitTetrahedron(AssetManager* assetManager) { return GetOrLoad(assetManager, unitTetrahedron, TetrahedronParams{.radius = 0.5f}); }
StaticModelHandle BuiltinAssets::GetUnitOctahedron(AssetManager* assetManager) { return GetOrLoad(assetManager, unitOctahedron, OctahedronParams{.radius = 0.5f}); }
StaticModelHandle BuiltinAssets::GetUnitIcosahedron(AssetManager* assetManager) { return GetOrLoad(assetManager, unitIcosahedron, IcosahedronParams{.radius = 0.5f}); }
StaticModelHandle BuiltinAssets::GetUnitDodecahedron(AssetManager* assetManager) { return GetOrLoad(assetManager, unitDodecahedron, DodecahedronParams{.radius = 0.5f}); }
} // Engine
