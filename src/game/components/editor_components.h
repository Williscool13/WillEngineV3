//
// Created by William on 2026-03-16.
//

#ifndef WILL_ENGINE_EDITOR_COMPONENTS_H
#define WILL_ENGINE_EDITOR_COMPONENTS_H
#include <array>

#include "core/stack_string.h"
#include "core/string_id.h"

namespace Game::Component
{
struct EntityFolderComponent
{
    /**
     * Up to 2 levels deep
     */
    std::array<StringID, 2> folderHierarchy;

    /**
     * Stack-based strings (max 16 char)
     */
    std::array<ShortString, 2> folderHierarchyNames;

};
}

#endif //WILL_ENGINE_EDITOR_COMPONENTS_H
