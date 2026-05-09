//
// Created by William on 2026-01-30.
//

#include "physics_debug_filter.h"

#ifdef JPH_DEBUG_RENDERER
namespace Physics
{
DebugDrawFilter::DebugDrawFilter(Core::MemoryManager& memoryManager)
    : bodiesToDraw(&memoryManager.PhysicsAligned(), Core::AllocTag::Physics, 128)
{}
} // Physics
#endif
