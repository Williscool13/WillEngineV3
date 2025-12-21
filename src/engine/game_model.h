//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_GAME_MODEL_H
#define WILL_ENGINE_GAME_MODEL_H
#include "core/math/transform.h"
#include "render/vulkan/vk_resource_manager.h"

namespace Game
{
/**
 * todo: move to game and set up ECS
 */

struct NodeInstance
{
    uint32_t parent{~0u};
    uint32_t originalNodeIndex{0};
    uint32_t depth{~0u};

    // Rigidbody
    uint32_t meshIndex{~0u};
    // Skeletal mesh
    // Data duplication here, but this way we don't need to look up the model data every time we update the transforms
    uint32_t jointMatrixIndex{0};
    glm::mat4 inverseBindMatrix{1.0f};


    Render::ModelEntryHandle modelMatrixHandle{Render::ModelEntryHandle::INVALID};
    std::vector<Render::InstanceEntryHandle> instanceEntryHandles{};

    Transform transform = Transform::IDENTITY;
    // populated when iterated upon at end of game frame
    glm::mat4 cachedWorldTransform{1.0f};

    explicit NodeInstance(const Render::Node& n);
};

struct ModelInstance
{
    Render::WillModelHandle modelEntryHandle{Render::WillModelHandle::INVALID};

    // sorted when generated
    std::vector<NodeInstance> nodes;

    std::vector<uint32_t> nodeRemap{};

    bool bNeedToSendToRender{false};
    Transform transform;
    OffsetAllocator::Allocation jointMatrixAllocation{};
    uint32_t jointMatrixOffset{0};
};

/*struct PrimitiveInstance
{

};

struct ModelInstance
{

};

struct MaterialInstance
{

};*/
} // Game

#endif //WILL_ENGINE_GAME_MODEL_H
