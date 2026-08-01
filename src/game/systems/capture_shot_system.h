//
// Created by William on 2026-08-01.
//

#ifndef WILL_ENGINE_CAPTURE_SHOT_SYSTEM_H
#define WILL_ENGINE_CAPTURE_SHOT_SYSTEM_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "render/interface/render_interface.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
enum class InputContext : uint8_t;
}

namespace Game
{
/**
 * Automated capture run: activated by --shots, teleports the editor camera through the shot list,
 * resets temporal history, settles N frames per pose, and writes named PNGs via the screenshot path.
 * Modeled on ProbeBakeSystem; ticked from GamePrepareFrame before FunctionKeyRenderUpdate so the
 * screenshot request lands on the same FrameBuffer.
 */
struct CaptureShotSystem
{
    enum class Phase : uint8_t
    {
        Idle,
        WaitReady,
        ShotSetup,
        Settling,
        CaptureRequested,
        AwaitSaved,
    };

    struct Shot
    {
        Core::InlineString<64> name{};
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        int32_t settleFrames{-1};
    };

    Phase phase{Phase::Idle};
    bool bActive{false};
    /** Latched after the run (or a shot-list load failure) so the system never re-arms in this session. */
    bool bDone{false};

    Core::InlineVector<Shot, 64> shots{};
    int32_t currentShot{0};
    int32_t settleCounter{0};
    int32_t settleFrames{120};
    int32_t readyQuietCounter{0};
    int32_t readyWaitedFrames{0};
    int32_t awaitFrames{0};
    bool bSawInFlight{false};
    Core::InlineString<512> outputDir{};
    Engine::InputContext stashedInputContext{};

    /** Advances the run one render frame; owns FrameBuffer::screenshotPath (cleared every tick, set on request frames). */
    void Tick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
};

/** Advances the capture run one render frame; call in GamePrepareFrame after ProbeBakeTick and before FunctionKeyRenderUpdate. */
void CaptureShotTick(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);

/** During an active run, empties debug draws, sprites, probe previews, selection outline, and GPU debug so none of it bakes into the PNGs. Call late in GamePrepareFrame after all debug producers. No-op otherwise. */
void CaptureShotScrubFrame(Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
} // Game

#endif //WILL_ENGINE_CAPTURE_SHOT_SYSTEM_H
