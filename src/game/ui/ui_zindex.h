//
// Created by William on 2026-07-15.
//

#ifndef WILL_ENGINE_UI_ZINDEX_H
#define WILL_ENGINE_UI_ZINDEX_H

#include <cstdint>

namespace Game::UI::ZIndex
{
/**
 * Central registry of Clay floating zIndex layers. Clay floating elements are independent, globally z-sorted tree
 * roots rather than painting above their parent's content, so every floating element needs a value from here
 * instead of a local literal, or two unrelated features can silently collide. An element nested inside a layer
 * (e.g. a window's own resize handle or caret) should offset from that layer's constant (LAYER + 1, LAYER + 10,
 * ...) rather than pick an unrelated number.
 */
inline constexpr int16_t LIST_DECORATION = 1; // Scroll thumbs and similar, floating within a list's own bounds
inline constexpr int16_t HUD_OVERLAY = 100; // Always-on-top HUD readouts (FPS counter, etc.)
inline constexpr int16_t CONSOLE_WINDOW = 200; // Dev console window
} // Game::UI::ZIndex

#endif //WILL_ENGINE_UI_ZINDEX_H
