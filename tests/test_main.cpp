//
// Created by William on 2026-08-12.
//

#include <catch2/catch_session.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "core/memory/memory_manager.h"
#include "engine/logging/log_category.h"
#include "meshoptimizer/src/meshoptimizer.h"
#include "par/par_shapes_ext.h"
#include "asset-load/asset-load-jobs/text3d_geometry.h"

// The engine wires these hooks in WillEngine/AsyncAssetLoadManager init; the test runner must install its own or any test touching meshopt/par_shapes/earcut null-derefs (or silently mallocs)
static Core::MemoryManager gTestMemory;

static void* TestMeshoptAlloc(size_t size)
{
    return gTestMemory.AssetsScratch().Alloc(size, Core::AllocTag::Meshopt);
}

static void TestMeshoptFree(void* ptr)
{
    gTestMemory.AssetsScratch().Free(ptr);
}

int main(int argc, char* argv[])
{
    gTestMemory.Init({
        .persistentSize = 8ull * 1024 * 1024,
        .assetsPoolSize = 8ull * 1024 * 1024,
        .physicsPoolSize = 8ull * 1024 * 1024,
        .renderPoolSize = 8ull * 1024 * 1024,
        .arenaPoolSize = 32ull * 1024 * 1024,
        .generalPoolSize = 8ull * 1024 * 1024,
        .generalPoolBudget = 64ull * 1024 * 1024,
        .assetsScratchPoolSize = 16ull * 1024 * 1024,
        .assetsScratchBudget = 512ull * 1024 * 1024,
    });

    meshopt_setAllocator(TestMeshoptAlloc, TestMeshoptFree);
    par_shapes_set_allocator(&gTestMemory.AssetsScratch());
    AssetLoad::SetEarcutAllocator(&gTestMemory.AssetsScratch());

    for (const char* name : Engine::kCategoryNames) {
        spdlog::stdout_color_mt(name);
    }

    return Catch::Session().run(argc, argv);
}
