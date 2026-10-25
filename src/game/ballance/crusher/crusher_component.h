//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_BALLANCE_CRUSHER_COMPONENT_H
#define WILL_ENGINE_BALLANCE_CRUSHER_COMPONENT_H

#include <cstdint>

#include "core/string_id.h"
#include "engine/component_registry.h"
#include "engine/core/entity_ref.h"
#include "engine/reflection/reflection.h"
#include "game/ballance/fwd_components.h"

namespace Ballance::Component
{
enum class CrusherPhase : uint32_t
{
    Waiting = 0,
    Slam = 1,
    Hold = 2,
    Rise = 3,
    Push = 4,
    Return = 5,
};

/**
 * On the crusher's root. A crate wholly under the head starts a cycle: the feeding belts stop, the head slams down `stroke` metres, holds, rises, then the pusher sweeps `pushDistance` along its local -X and returns, and the belts start again.
 * Every intact crate the head comes down on breaks just before contact, and so does any the pusher meets.
 * The parts are entity references: the head moves along its own -Y, the pusher along its own -X, and the arm stretches in Y from a pivot at its top to follow the head.
 */
struct CrusherComponent
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "CrusherComponent";
    static constexpr bool MOVES_ENTITY = true;

    Engine::EntityRef head;
    Engine::EntityRef arm;
    Engine::EntityRef pusher;
    Engine::EntityRef beltA;
    Engine::EntityRef beltB;

    float stroke{17.0f};
    float slamSpeed{20.0f};
    float holdTime{0.4f};
    float riseSpeed{3.0f};
    float pushDistance{6.9f};
    float pushSpeed{3.0f};
    float returnSpeed{3.0f};
    /** Seconds parked at the top before another crate can start a cycle. */
    float restTime{2.0f};

    WILL_REFLECT(CrusherComponent,
        WILL_FIELD(head),
        WILL_FIELD(arm),
        WILL_FIELD(pusher),
        WILL_FIELD(beltA),
        WILL_FIELD(beltB),
        WILL_FIELD(stroke, .min = 0.0f, .speed = 0.05f),
        WILL_FIELD(slamSpeed, .min = 0.01f, .speed = 0.1f),
        WILL_FIELD(holdTime, .min = 0.0f, .speed = 0.05f),
        WILL_FIELD(riseSpeed, .min = 0.01f, .speed = 0.1f),
        WILL_FIELD(pushDistance, .min = 0.0f, .speed = 0.05f),
        WILL_FIELD(pushSpeed, .min = 0.01f, .speed = 0.1f),
        WILL_FIELD(returnSpeed, .min = 0.01f, .speed = 0.1f),
        WILL_FIELD(restTime, .min = 0.0f, .speed = 0.05f))
};

struct CrusherRuntime
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "CrusherRuntime";

    CrusherPhase phase{CrusherPhase::Waiting};
    float phaseTime{0.0f};
    /** Metres the head is below its parked position. */
    float drop{0.0f};
    /** Metres the pusher is out from its home position. */
    float pushed{0.0f};

    WILL_REFLECT(CrusherRuntime, WILL_FIELD(phase), WILL_FIELD(phaseTime), WILL_FIELD(drop), WILL_FIELD(pushed))
};

/**
 * Closes once more than `closeShards` shards are awake: crate feeders stop, and crushers stop their belts and start no new cycle.
 * Opens again at `openShards` or fewer, and stays open at least `minOpenSeconds`: restarting the belts wakes the shards resting on them.
 */
struct ThroatValveComponent
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "ThroatValveComponent";

    int32_t closeShards{480};
    int32_t openShards{320};
    float minOpenSeconds{3.0f};

    WILL_REFLECT(ThroatValveComponent,
        WILL_FIELD(closeShards),
        WILL_FIELD(openShards),
        WILL_FIELD(minOpenSeconds, .min = 0.0f, .speed = 0.1f))
};

struct ThroatValveRuntime
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "ThroatValveRuntime";

    bool bClosed{false};
    float openTime{0.0f};

    WILL_REFLECT(ThroatValveRuntime, WILL_FIELD(bClosed), WILL_FIELD(openTime))
};

/**
 * Spawns `prefabId` at `cratesPerSecond`, at a random point on the ring between `minRadius` and `maxRadius` round this entity at its height, in a random orientation.
 * Holds while any throat valve is closed or `maxIntact` intact breakables exist.
 */
struct CrateFeederComponent
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "CrateFeederComponent";

    StringID prefabId;
    float cratesPerSecond{1.0f};
    float minRadius{25.0f};
    float maxRadius{28.0f};
    int32_t maxIntact{48};

    WILL_REFLECT(CrateFeederComponent,
        WILL_FIELD(prefabId),
        WILL_FIELD(cratesPerSecond, .min = 0.0f, .speed = 0.05f),
        WILL_FIELD(minRadius, .min = 0.0f, .speed = 0.1f),
        WILL_FIELD(maxRadius, .min = 0.0f, .speed = 0.1f),
        WILL_FIELD(maxIntact))
};

struct CrateFeederRuntime
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "CrateFeederRuntime";

    float timer{0.0f};

    WILL_REFLECT(CrateFeederRuntime, WILL_FIELD(timer))
};
}

#endif //WILL_ENGINE_BALLANCE_CRUSHER_COMPONENT_H
