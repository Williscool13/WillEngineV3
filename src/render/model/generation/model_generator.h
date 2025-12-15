//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_GENERATOR_H
#define WILL_ENGINE_MODEL_GENERATOR_H
#include <filesystem>

namespace Render
{
class ModelGenerator
{
    static void LoadGltf(const std::filesystem::path& source);
};
} // Render

#endif //WILL_ENGINE_MODEL_GENERATOR_H