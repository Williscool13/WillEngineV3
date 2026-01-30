//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_DEBUG_DRAW_FILTER_H
#define WILL_ENGINE_DEBUG_DRAW_FILTER_H

#ifdef JPH_DEBUG_RENDERER

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <vector>
#include <algorithm>

#include "Jolt/Physics/Body/BodyFilter.h"

namespace Physics
{

class DebugDrawFilter final : public JPH::BodyDrawFilter
{
public:
    ~DebugDrawFilter() override = default;

    bool ShouldDraw(const JPH::Body& inBody) const override
    {
        return !bodiesToDraw.empty() && 
               std::ranges::find(bodiesToDraw, inBody.GetID()) != bodiesToDraw.end();
    }

    void AddBody(const JPH::BodyID bodyId)
    {
        if (std::ranges::find(bodiesToDraw, bodyId) == bodiesToDraw.end()) {
            bodiesToDraw.push_back(bodyId);
        }
    }

    void RemoveBody(const JPH::BodyID bodyId)
    {
        const auto it = std::ranges::find(bodiesToDraw, bodyId);
        if (it != bodiesToDraw.end()) {
            bodiesToDraw.erase(it);
        }
    }

    void Clear()
    {
        bodiesToDraw.clear();
    }

private:
    std::vector<JPH::BodyID> bodiesToDraw;
};

} // Physics

#endif // JPH_DEBUG_RENDERER

#endif // WILL_ENGINE_DEBUG_DRAW_FILTER_H