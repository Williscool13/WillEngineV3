//
// Created by William on 2026-06-14.
//

#ifndef WILL_ENGINE_BUILTIN_ASSETS_H
#define WILL_ENGINE_BUILTIN_ASSETS_H

#include "asset_manager_types.h"

namespace Engine
{
class IAssetLoader;

/**
 * Owns engine-provided built-in assets that are generated on demand rather than authored on disk. Handles are loaded lazily on first request and kept resident for the engine's lifetime.
 * Currently primitive meshes, built-in textures, and similar are expected to live here as it grows.
 * Lazy-loaded.
 */
class BuiltinAssets
{
public:
    StaticModelHandle GetUnitQuad(IAssetLoader* assetLoader); // 1x1 XZ plane, +Y normal

    StaticModelHandle GetUnitDisk(IAssetLoader* assetLoader);

    StaticModelHandle GetUnitCube(IAssetLoader* assetLoader);

    StaticModelHandle GetUnitSphere(IAssetLoader* assetLoader);

    StaticModelHandle GetUnitCylinder(IAssetLoader* assetLoader);

    StaticModelHandle GetUnitCone(IAssetLoader* assetLoader);

    StaticModelHandle GetUnitCapsule(IAssetLoader* assetLoader);

    StaticModelHandle GetUnitTorus(IAssetLoader* assetLoader);

    StaticModelHandle GetUnitHemisphere(IAssetLoader* assetLoader);

    StaticModelHandle GetUnitTetrahedron(IAssetLoader* assetLoader);

    StaticModelHandle GetUnitOctahedron(IAssetLoader* assetLoader);

    StaticModelHandle GetUnitIcosahedron(IAssetLoader* assetLoader);

    StaticModelHandle GetUnitDodecahedron(IAssetLoader* assetLoader);

private:
    StaticModelHandle unitQuad{};
    StaticModelHandle unitDisk{};
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
