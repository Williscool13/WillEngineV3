//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_INPUT_UTILS_H
#define WILL_ENGINE_INPUT_UTILS_H

#include <SDL3/SDL.h>

#include "input_frame.h"

namespace Core
{
inline Key SDLKeyToKey(SDL_Keycode sdl)
{
    switch (sdl)
    {
        case SDLK_A: return Key::A;
        case SDLK_B: return Key::B;
        case SDLK_C: return Key::C;
        case SDLK_D: return Key::D;
        case SDLK_E: return Key::E;
        case SDLK_F: return Key::F;
        case SDLK_G: return Key::G;
        case SDLK_H: return Key::H;
        case SDLK_I: return Key::I;
        case SDLK_J: return Key::J;
        case SDLK_K: return Key::K;
        case SDLK_L: return Key::L;
        case SDLK_M: return Key::M;
        case SDLK_N: return Key::N;
        case SDLK_O: return Key::O;
        case SDLK_P: return Key::P;
        case SDLK_Q: return Key::Q;
        case SDLK_R: return Key::R;
        case SDLK_S: return Key::S;
        case SDLK_T: return Key::T;
        case SDLK_U: return Key::U;
        case SDLK_V: return Key::V;
        case SDLK_W: return Key::W;
        case SDLK_X: return Key::X;
        case SDLK_Y: return Key::Y;
        case SDLK_Z: return Key::Z;

        case SDLK_0: return Key::NUM_0;
        case SDLK_1: return Key::NUM_1;
        case SDLK_2: return Key::NUM_2;
        case SDLK_3: return Key::NUM_3;
        case SDLK_4: return Key::NUM_4;
        case SDLK_5: return Key::NUM_5;
        case SDLK_6: return Key::NUM_6;
        case SDLK_7: return Key::NUM_7;
        case SDLK_8: return Key::NUM_8;
        case SDLK_9: return Key::NUM_9;

        case SDLK_F1:  return Key::F1;
        case SDLK_F2:  return Key::F2;
        case SDLK_F3:  return Key::F3;
        case SDLK_F4:  return Key::F4;
        case SDLK_F5:  return Key::F5;
        case SDLK_F6:  return Key::F6;
        case SDLK_F7:  return Key::F7;
        case SDLK_F8:  return Key::F8;
        case SDLK_F9:  return Key::F9;
        case SDLK_F10: return Key::F10;
        case SDLK_F11: return Key::F11;
        case SDLK_F12: return Key::F12;

        case SDLK_UP:    return Key::UP;
        case SDLK_DOWN:  return Key::DOWN;
        case SDLK_LEFT:  return Key::LEFT;
        case SDLK_RIGHT: return Key::RIGHT;

        case SDLK_LCTRL:  return Key::LCTRL;
        case SDLK_LSHIFT: return Key::LSHIFT;
        case SDLK_LALT:   return Key::LALT;
        case SDLK_LGUI:   return Key::LGUI;
        case SDLK_RCTRL:  return Key::RCTRL;
        case SDLK_RSHIFT: return Key::RSHIFT;
        case SDLK_RALT:   return Key::RALT;
        case SDLK_RGUI:   return Key::RGUI;

        case SDLK_HOME:     return Key::HOME;
        case SDLK_END:      return Key::END;
        case SDLK_PAGEUP:   return Key::PAGEUP;
        case SDLK_PAGEDOWN: return Key::PAGEDOWN;
        case SDLK_INSERT:   return Key::INSERT;

        case SDLK_ESCAPE:        return Key::ESCAPE;
        case SDLK_RETURN:        return Key::RETURN;
        case SDLK_BACKSPACE:     return Key::BACKSPACE;
        case SDLK_TAB:           return Key::TAB;
        case SDLK_SPACE:         return Key::SPACE;
        case SDLK_PRINTSCREEN:   return Key::PRINTSCREEN;
        case SDLK_SCROLLLOCK:    return Key::SCROLLLOCK;
        case SDLK_PAUSE:         return Key::PAUSE;
        case SDLK_CAPSLOCK:      return Key::CAPSLOCK;
        case SDLK_NUMLOCKCLEAR:  return Key::NUMLOCKCLEAR;

        case SDLK_PERIOD:       return Key::PERIOD;
        case SDLK_COMMA:        return Key::COMMA;
        case SDLK_SEMICOLON:    return Key::SEMICOLON;
        case SDLK_APOSTROPHE:   return Key::APOSTROPHE;
        case SDLK_SLASH:        return Key::SLASH;
        case SDLK_BACKSLASH:    return Key::BACKSLASH;
        case SDLK_MINUS:        return Key::MINUS;
        case SDLK_EQUALS:       return Key::EQUALS;
        case SDLK_LEFTBRACKET:  return Key::LEFTBRACKET;
        case SDLK_RIGHTBRACKET: return Key::RIGHTBRACKET;
        case SDLK_GRAVE:        return Key::BACKTICK;

        default: return Key::UNKNOWN;
    }
}

inline MouseButton SDLMouseButtonToMouseButton(uint8_t sdlButton)
{
    switch (sdlButton)
    {
        case SDL_BUTTON_LEFT:   return MouseButton::LMB;
        case SDL_BUTTON_MIDDLE: return MouseButton::MMB;
        case SDL_BUTTON_RIGHT:  return MouseButton::RMB;
        case SDL_BUTTON_X1:     return MouseButton::X1;
        case SDL_BUTTON_X2:     return MouseButton::X2;
        default:                return MouseButton::LMB; // Shouldn't happen
    }
}

inline void UpdateButtonState(InputFrame::ButtonState& button, bool isPressed)
{
    if (!button.down && isPressed) {
        button.pressed = true;
    }
    if (button.down && !isPressed) {
        button.released = true;
    }
    button.down = isPressed;
}

} // Core

#endif //WILL_ENGINE_INPUT_UTILS_H