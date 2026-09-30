/* Keyboard key codes for plat_key_down(). The values are USB HID keyboard usages (page 0x07), which
   are also SDL scancode values, so the SDL backend passes them straight through (platform_sdl.c checks
   this at compile time) and other backends map their key events onto the same numbers. Only the keys
   the game reads are listed. */
#pragma once

enum {
    KEY_A = 4, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_1 = 30, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
    KEY_RETURN = 40, KEY_ESCAPE = 41, KEY_BACKSPACE = 42, KEY_TAB = 43, KEY_SPACE = 44,
    KEY_LEFTBRACKET = 47, KEY_RIGHTBRACKET = 48,
    KEY_F1 = 58, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_INSERT = 73, KEY_HOME = 74, KEY_PAGEUP = 75, KEY_DELETE = 76, KEY_END = 77, KEY_PAGEDOWN = 78,
    KEY_RIGHT = 79, KEY_LEFT = 80, KEY_DOWN = 81, KEY_UP = 82,
    KEY_KP_DIVIDE = 84, KEY_KP_MULTIPLY = 85, KEY_KP_MINUS = 86, KEY_KP_PLUS = 87, KEY_KP_ENTER = 88,
    KEY_KP_1 = 89, KEY_KP_2, KEY_KP_3, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9, KEY_KP_0,
    KEY_KP_PERIOD = 99,
    KEY_LCTRL = 224, KEY_LSHIFT = 225, KEY_LALT = 226, KEY_LGUI = 227,
    KEY_RCTRL = 228, KEY_RSHIFT = 229, KEY_RALT = 230, KEY_RGUI = 231,
    KEY_COUNT = 512
};
