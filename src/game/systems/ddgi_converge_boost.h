//
// Created by William on 2026-07-23.
//

#ifndef WILL_ENGINE_DDGI_CONVERGE_BOOST_H
#define WILL_ENGINE_DDGI_CONVERGE_BOOST_H

#include "engine/engine_api.h"
#include "render/interface/render_interface.h"

namespace Game
{
constexpr int32_t DDGI_CONVERGE_SUPER_BOOST_FRAMES = 100;
constexpr int32_t DDGI_CONVERGE_BOOST_FRAMES = 200;

inline void DDGIConvergeBoostTrigger(Engine::DDGIConvergeBoost& boost, const Core::DDGIParams& ddgi)
{
    if (!boost.bActive) {
        boost.stashedHysteresis = ddgi.hysteresis;
        boost.stashedVisibilityHysteresis = ddgi.visibilityHysteresis;
        boost.stashedRaysPerProbe = ddgi.raysPerProbe;
        boost.stashedOuterRaysPerProbe = ddgi.outerRaysPerProbe;
        boost.stashedWorldCacheShadeInterval = ddgi.worldCacheShadeInterval;
        boost.stashedWorldCacheAccumCap = ddgi.worldCacheAccumCap;
        boost.bActive = true;
    }
    boost.frame = 0;
}

inline void DDGIConvergeBoostTick(Engine::DDGIConvergeBoost& boost, Core::DDGIParams& ddgi)
{
    if (!boost.bActive) {
        return;
    }

    if (boost.frame < DDGI_CONVERGE_SUPER_BOOST_FRAMES) {
        ddgi.hysteresis = 0.2f;
        ddgi.visibilityHysteresis = 0.5f;
        ddgi.raysPerProbe = DDGI_MAX_RAYS_PER_PROBE;
        ddgi.outerRaysPerProbe = DDGI_MAX_RAYS_PER_PROBE;
        ddgi.worldCacheShadeInterval = 2;
        ddgi.worldCacheAccumCap = 1;
    } else if (boost.frame < DDGI_CONVERGE_BOOST_FRAMES) {
        ddgi.hysteresis = 0.9f;
        ddgi.visibilityHysteresis = 0.9f;
        ddgi.raysPerProbe = DDGI_MAX_RAYS_PER_PROBE;
        ddgi.outerRaysPerProbe = DDGI_MAX_RAYS_PER_PROBE;
        ddgi.worldCacheShadeInterval = 4;
        ddgi.worldCacheAccumCap = 4;
    } else {
        ddgi.hysteresis = boost.stashedHysteresis;
        ddgi.visibilityHysteresis = boost.stashedVisibilityHysteresis;
        ddgi.raysPerProbe = boost.stashedRaysPerProbe;
        ddgi.outerRaysPerProbe = boost.stashedOuterRaysPerProbe;
        ddgi.worldCacheShadeInterval = boost.stashedWorldCacheShadeInterval;
        ddgi.worldCacheAccumCap = boost.stashedWorldCacheAccumCap;
        boost.bActive = false;
        return;
    }
    boost.frame += 1;
}
} // Game

#endif //WILL_ENGINE_DDGI_CONVERGE_BOOST_H
