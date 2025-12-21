//
// Created by William on 2025-12-21.
//

#include "game_model.h"

namespace Game
{
NodeInstance::NodeInstance(const Render::Node& n)
{
    parent = n.parent;
    depth = n.depth;
    meshIndex = n.meshIndex;
    transform = {n.localTranslation, n.localRotation, n.localScale};
    jointMatrixIndex = n.inverseBindIndex;
}
} // Game