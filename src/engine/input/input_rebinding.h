//
// Created by William on 2026-07-06.
//

#ifndef WILL_ENGINE_INPUT_REBINDING_H
#define WILL_ENGINE_INPUT_REBINDING_H

#include <cstdint>
#include "core/containers/inline_vector.h"
#include "engine/core/action_handle.h"

namespace Core
{
struct InputFrame;
}

namespace Engine
{
struct InputState;
struct BindingSource;

/**
 * Row indices into InputState::bindings whose action matches.
 */
Core::InlineVector<size_t, 8> EnumerateBindingRows(const InputState& input, ActionHandle action);

/**
 * Suppress all other inputs while capturing what the user is changing their inputs to.
 */
void BeginCapture(InputState& input, size_t bindingRow);

/**
 * Captures first pressed key/mouse/gamepad button and rewrites target row.
 */
void PollCapture(InputState& input, const Core::InputFrame& frame);

/**
 * Overwrites a Discrete binding row's source in-place; no-ops for non-Discrete rows. Marks bindings dirty.
 */
void RewriteBinding(InputState& input, size_t bindingRow, BindingSource newSource);

/**
 * Copies input.defaultBindings into input.bindings verbatim. Does NOT mark bindings dirty -
 * this is bootstrapping (registration, config-apply baseline), not a user rebind to persist.
 */
void ApplyDefaultBindings(InputState& input);

/**
 * Restores a single row to its registered default. Marks bindings dirty.
 */
void ResetBindingToDefault(InputState& input, size_t bindingRow);
} // Engine

#endif //WILL_ENGINE_INPUT_REBINDING_H
