//
// Created by William on 2026-07-15.
//

#ifndef WILL_ENGINE_UI_ZINDEX_H
#define WILL_ENGINE_UI_ZINDEX_H

#include <cstdint>

namespace Engine::UI::ZIndex
{
inline constexpr int16_t LIST_DECORATION = 1; // Scroll thumbs and similar, floating within a list's own bounds
inline constexpr int16_t HUD_OVERLAY = 100; // Always-on-top HUD readouts (FPS counter, etc.)
inline constexpr int16_t CONSOLE_WINDOW = 200; // Dev console window
} // Engine::UI::ZIndex

#endif //WILL_ENGINE_UI_ZINDEX_H
