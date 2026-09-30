//
// Created by William on 2026-07-13.
//

#ifndef WILL_ENGINE_EDITOR_MULTI_EDIT_H
#define WILL_ENGINE_EDITOR_MULTI_EDIT_H

#include <random>

#include "imgui.h"

#include "core/containers/inline_string.h"

namespace Engine::MultiEdit
{
enum class FieldAction { None, Drag, Commit };

struct FieldResult
{
    FieldAction action = FieldAction::None;
    float dragDelta = 0.0f;
    char expr[64] = {};
};

/**
 * One axis of a multi-edit transform field: a colored strip that acts as a relative drag handle, plus a text/expression input.
 * Shows uniformValue when the selection agrees on this axis, or a "..." hint when mixed. Dragging the strip yields a per-frame
 * relative delta; pressing Enter yields the raw expression to feed EvaluateFloatField. width is the total field width incl. strip.
 */
FieldResult ScalarField(const char* id, ImU32 axisColor, float uniformValue, bool mixed, float dragSpeed, float width);
/** True if the name template contains a `{S}` or `{R(a,b)}` token. */
bool ContainsNameToken(const char* s);

/** Expands `{S}` to the 0-based selection index and `{R(a,b)}` to a random int in [a,b); anything else is copied as is. */
void ExpandNameTemplate(Core::InlineString<128>& dst, const char* templ, int index, std::mt19937_64& rng);

/**
 * Evaluates a transform-field expression for one entity: + - * / with the usual precedence, parentheses and unary minus over
 * number | x (current value) | S (0-based selection index) | R(a,b) (random float in [a,b)). A leading * or / applies to x.
 * Returns false (leaving out untouched) on malformed input, division by zero or a non-finite result.
 */
bool EvaluateFloatField(const char* expr, float currentValue, int index, std::mt19937_64& rng, float& out);
}

#endif //WILL_ENGINE_EDITOR_MULTI_EDIT_H
