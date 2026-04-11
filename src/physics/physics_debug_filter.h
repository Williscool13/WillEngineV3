//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_DEBUG_DRAW_FILTER_H
#define WILL_ENGINE_DEBUG_DRAW_FILTER_H

#ifdef JPH_DEBUG_RENDERER

#include <algorithm>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>

#include "core/memory/memory_manager.h"
#include "core/containers/vector.h"


namespace Physics
{

class DebugDrawFilter final : public JPH::BodyDrawFilter
{
public:
    DebugDrawFilter() = default;
    explicit DebugDrawFilter(Core::MemoryManager& memoryManager);
    ~DebugDrawFilter() override = default;

    [[nodiscard]] bool ShouldDraw(const JPH::Body& inBody) const override
    {
        return !bodiesToDraw.IsEmpty() &&
               std::ranges::find(bodiesToDraw, inBody.GetID()) != bodiesToDraw.end();
    }

    void AddBody(const JPH::BodyID bodyId)
    {
        if (std::ranges::find(bodiesToDraw, bodyId) == bodiesToDraw.end()) {
            bodiesToDraw.PushBack(bodyId);
        }
    }

    void RemoveBody(const JPH::BodyID bodyId)
    {
        const auto it = std::ranges::find(bodiesToDraw, bodyId);
        if (it != bodiesToDraw.end()) {
            bodiesToDraw.Remove(it);
        }
    }

    void Clear()
    {
        bodiesToDraw.Clear();
    }

private:
    Core::Vector<JPH::BodyID> bodiesToDraw;
};

} // Physics

#endif // JPH_DEBUG_RENDERER

#endif // WILL_ENGINE_DEBUG_DRAW_FILTER_H