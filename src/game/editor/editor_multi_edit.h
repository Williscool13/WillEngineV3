//
// Created by William on 2026-07-13.
//

#ifndef WILL_ENGINE_EDITOR_MULTI_EDIT_H
#define WILL_ENGINE_EDITOR_MULTI_EDIT_H

#include <random>

#include "imgui.h"

#include "core/containers/inline_string.h"

namespace Game::MultiEdit
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
/** True if the name template contains a `_S` or `_R(a,b)` token (so the caller expands per-entity instead of broadcasting a literal). */
bool ContainsNameToken(const char* s);

/**
 * Expands `_S` (0-based selection index) and `_R(a,b)` (random int in [a,b)) tokens; the leading underscore is kept as a
 * separator, so `Crate_S` -> `Crate_0` and `Enemy_R(0,5)` -> `Enemy_3`. `_S` only counts when the S is standalone (followed
 * by end or a non-letter), so ordinary names like `weapon_Sword` are untouched. Result is written into dst (truncated to capacity).
 */
void ExpandNameTemplate(Core::InlineString<128>& dst, const char* templ, int index, std::mt19937_64& rng);

/**
 * Evaluates a transform-field expression for one entity. Grammar: field := term [op term], op := + - * /,
 * term := number | x | S | R(a,b). A signed number is absolute; leading `*`/`/` imply x as lhs (relative);
 * `x` is the entity's current value, `S` its 0-based selection index, `R(a,b)` a random float in [a,b).
 * Returns false (leaving out untouched) on malformed input or division by zero.
 */
bool EvaluateFloatField(const char* expr, float currentValue, int index, std::mt19937_64& rng, float& out);
}

#endif //WILL_ENGINE_EDITOR_MULTI_EDIT_H
