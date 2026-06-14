//
// Created by William on 2026-06-14.
//

#ifndef WILL_ENGINE_BUILTIN_ASSETS_H
#define WILL_ENGINE_BUILTIN_ASSETS_H

#include "asset_manager_types.h"

namespace Engine
{
class AssetManager;

/**
 * Owns engine-provided built-in assets that are generated on demand rather than authored on disk. Handles are loaded lazily on first request and kept resident for the engine's lifetime.
 * Currently primitive meshes, built-in textures, and similar are expected to live here as it grows.
 * Lazy-loaded.
 */
class BuiltinAssets
{
public:
    StaticModelHandle GetUnitQuad(AssetManager* assetManager); // 1x1 XZ plane, +Y normal

    StaticModelHandle GetUnitCube(AssetManager* assetManager);

    StaticModelHandle GetUnitSphere(AssetManager* assetManager);

    StaticModelHandle GetUnitCylinder(AssetManager* assetManager);

    StaticModelHandle GetUnitCone(AssetManager* assetManager);

    StaticModelHandle GetUnitCapsule(AssetManager* assetManager);

    StaticModelHandle GetUnitTorus(AssetManager* assetManager);

    StaticModelHandle GetUnitHemisphere(AssetManager* assetManager);

    StaticModelHandle GetUnitTetrahedron(AssetManager* assetManager);

    StaticModelHandle GetUnitOctahedron(AssetManager* assetManager);

    StaticModelHandle GetUnitIcosahedron(AssetManager* assetManager);

    StaticModelHandle GetUnitDodecahedron(AssetManager* assetManager);

private:
    StaticModelHandle unitQuad{};
    StaticModelHandle unitCube{};
    StaticModelHandle unitSphere{};
    StaticModelHandle unitCylinder{};
    StaticModelHandle unitCone{};
    StaticModelHandle unitCapsule{};
    StaticModelHandle unitTorus{};
    StaticModelHandle unitHemisphere{};
    StaticModelHandle unitTetrahedron{};
    StaticModelHandle unitOctahedron{};
    StaticModelHandle unitIcosahedron{};
    StaticModelHandle unitDodecahedron{};
};
} // Engine

#endif //WILL_ENGINE_BUILTIN_ASSETS_H
