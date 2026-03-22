//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_WILL_MODEL_ASSET_H
#define WILL_ENGINE_WILL_MODEL_ASSET_H

#include <cfloat>
#include <filesystem>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include "model_types.h"
#include "TaskScheduler.h"
#include "core/include/render_interface.h"
#include "engine/core/model_id.h"


namespace Engine
{
struct BoundingSphere
{
    glm::vec3 center{0.f};
    float radius{0.f};
};

struct AABB
{
    glm::vec3 min{FLT_MAX, FLT_MAX, FLT_MAX};
    glm::vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};

    glm::vec3 Center() const { return (min + max) * 0.5f; }
    glm::vec3 HalfExtents() const { return (max - min) * 0.5f; }
    float SurfaceArea() const { glm::vec3 d = max - min; return 2.f * (d.x * d.y + d.y * d.z + d.z * d.x); }
    float Volume() const { glm::vec3 d = max - min; return d.x * d.y * d.z; }
};

struct OBB
{
    glm::vec3 center{0.f};
    glm::vec3 halfExtents{0.f};
    glm::quat orientation{1.f, 0.f, 0.f, 0.f};
};

struct ModelBounds
{
    BoundingSphere sphere{};
    AABB aabb{};
    OBB obb{};
    glm::vec3 centroid{0.f};
    int dominantAxis{1};
    float surfaceArea{0.f};
};

struct StaticModel
{
public:
    enum class ModelLoadState
    {
        NotLoaded,
        Loaded,
        FailedToLoad
    };
public:
    StaticModel();

    ~StaticModel();

    StaticModel(const StaticModel&) = delete;

    StaticModel& operator=(const StaticModel&) = delete;

    StaticModel(StaticModel&&) noexcept = default;

    StaticModel& operator=(StaticModel&&) noexcept = default;

    // Populated by AssetManager, never changed
    std::string name{};
    StaticModelHandle selfHandle;
    /**
     * RNG for gltf models. Hash for procedural.
     */
    Engine::ModelID modelId{};
    ModelLoadState modelLoadState{ModelLoadState::NotLoaded};
    uint64_t acquireFrame{UINT64_MAX};
    uint64_t retireFrame{0};

    // Populated by AssetManager, Only for normal models
    std::filesystem::path source{};

    // Populated by AssetManager, Only for (simple) procedural models
    ProceduralParams proceduralParams{};

    // Populated by AssetManager, sometimes changed
    uint32_t refCount = 0;

    // Populated in AssetLoadThread
    StaticModelData modelData{};
    std::vector<Core::BufferAcquireOperation> bufferAcquireOps{};
    std::vector<Core::ImageAcquireOperation> imageAcquireOps{};

    struct PhysicsCache
    {
        std::vector<glm::vec3> positions;
        std::vector<uint32_t>  indices;
    };
    std::optional<PhysicsCache> physicsCache;

    // Populated by AssetManager, Only for spline models
    std::optional<SplineParams> splineParams{};

    ModelBounds bounds{};

    static ModelBounds ComputeBounds(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>* indices = nullptr);
};
} // AssetLoad

#endif //WILL_ENGINE_WILL_MODEL_ASSET_H
