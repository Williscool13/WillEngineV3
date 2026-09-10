//
// Created by William on 2026-09-09.
//

#ifndef WILL_ENGINE_PLAYTEST_SYSTEM_H
#define WILL_ENGINE_PLAYTEST_SYSTEM_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "engine/core/action_handle.h"
#include "render/interface/render_interface.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
enum class InputContext : uint8_t;
}

namespace Engine
{
struct CameraOverride
{
    enum class Mode : uint8_t
    {
        None,
        Held,
        Track,
    };

    Mode mode{Mode::None};
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

/**
 * Runs a .wplay script in file order. Armed by --play or the scene browser's Run button; a script without play events is a plain shot list.
 * Waits count physics steps while playing and render frames otherwise.
 */
struct PlaytestSystem
{
    enum class Phase : uint8_t
    {
        Idle,
        WaitReady,
        Step,
        Waiting,
        Capturing,
        AwaitSaved,
    };

    enum class Op : uint8_t
    {
        Cam,
        Play,
        Axis,
        Button,
        Wait,
        Reset,
        Capture,
        Fps,
        Console,
    };

    enum class CamMode : uint8_t
    {
        Follow,
        Held,
        Track,
    };

    struct Event
    {
        Op op{Op::Wait};
        CamMode camMode{CamMode::Follow};
        bool bFlag{false};
        int32_t count{0};
        int32_t fps{0};
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec2 axis{0.0f};
        ActionHandle action{};
        Core::InlineString<64> name{};
    };

    static constexpr uint32_t MAX_EVENTS = 256;

    Phase phase{Phase::Idle};
    bool bActive{false};
    bool bSkipCaptures{false};
    bool bCliRun{false};
    bool bCliConsumed{false};

    /** Read by the player controller and the game loop; both fall back to their defaults when None / 0. */
    CameraOverride cameraOverride{};
    int32_t frameLimit{0};

    Core::InlineString<512> pendingPath{};
    Core::InlineString<128> runName{};
    Core::InlineVector<Event, MAX_EVENTS> events{};
    int32_t cursor{0};
    int32_t captureCount{0};
    int32_t burstRemaining{0};
    int32_t burstIndex{0};
    int32_t fpsCap{0};
    int32_t waitCounter{0};
    uint64_t waitStepBase{0};
    int32_t readyQuietCounter{0};
    int32_t readyWaitedFrames{0};
    int32_t awaitFrames{0};
    bool bSawInFlight{false};
    Core::InlineString<512> outputDir{};
    Engine::InputContext stashedInputContext{};
    glm::vec3 stashedCameraTranslation{0.0f};
    glm::quat stashedCameraRotation{1.0f, 0.0f, 0.0f, 0.0f};

    /** Queues a run; it starts on the next tick once no probe bake or other run is active. */
    void Arm(const char* path) { pendingPath = Core::InlineString<512>(std::string_view(path)); }

    void Tick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
};

size_t CountLoadingEntities(Engine::EngineState* state);

void PlaytestTick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);

/** Empties debug draws, sprites, probe previews, selection outline, and GPU debug so none of it bakes into the PNGs. */
void PlaytestScrubFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
} // Engine

#endif //WILL_ENGINE_PLAYTEST_SYSTEM_H
