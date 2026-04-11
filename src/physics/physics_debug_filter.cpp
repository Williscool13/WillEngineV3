//
// Created by William on 2026-01-30.
//

#include "physics_debug_filter.h"

namespace Physics
{
DebugDrawFilter::DebugDrawFilter(Core::MemoryManager& memoryManager)
    : bodiesToDraw(&memoryManager.Physics(), Core::AllocTag::Physics, 128)
{}
} // Physics
