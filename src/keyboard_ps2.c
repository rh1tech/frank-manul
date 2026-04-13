/*
 * Iris 2350 - PS/2 Keyboard (via frank-wolf ps2 driver)
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * Uses the PIO-based PS/2 driver from frank-wolf.
 * Translates PS/2 scancode set 2 to HID keycodes and feeds them
 * to keyboard_key_change().
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "keyboard_ps2.h"
#include "keyboard.h"
#include "board_config.h"
#include "ps2.h"
#include "hardware/pio.h"
#include <stdbool.h>
#include <stdio.h>

/* PS/2 Scancode Set 2 to HID keycode translation table */
static const uint8_t ps2_to_hid[256] = {
    [0x1C] = 0x04, /* A */
    [0x32] = 0x05, /* B */
    [0x21] = 0x06, /* C */
    [0x23] = 0x07, /* D */
    [0x24] = 0x08, /* E */
    [0x2B] = 0x09, /* F */
    [0x34] = 0x0A, /* G */
    [0x33] = 0x0B, /* H */
    [0x43] = 0x0C, /* I */
    [0x3B] = 0x0D, /* J */
    [0x42] = 0x0E, /* K */
    [0x4B] = 0x0F, /* L */
    [0x3A] = 0x10, /* M */
    [0x31] = 0x11, /* N */
    [0x44] = 0x12, /* O */
    [0x4D] = 0x13, /* P */
    [0x15] = 0x14, /* Q */
    [0x2D] = 0x15, /* R */
    [0x1B] = 0x16, /* S */
    [0x2C] = 0x17, /* T */
    [0x3C] = 0x18, /* U */
    [0x2A] = 0x19, /* V */
    [0x1D] = 0x1A, /* W */
    [0x22] = 0x1B, /* X */
    [0x35] = 0x1C, /* Y */
    [0x1A] = 0x1D, /* Z */
    [0x16] = 0x1E, /* 1 */
    [0x1E] = 0x1F, /* 2 */
    [0x26] = 0x20, /* 3 */
    [0x25] = 0x21, /* 4 */
    [0x2E] = 0x22, /* 5 */
    [0x36] = 0x23, /* 6 */
    [0x3D] = 0x24, /* 7 */
    [0x3E] = 0x25, /* 8 */
    [0x46] = 0x26, /* 9 */
    [0x45] = 0x27, /* 0 */
    [0x5A] = 0x28, /* Enter */
    [0x76] = 0x29, /* Escape */
    [0x66] = 0x2A, /* Backspace */
    [0x0D] = 0x2B, /* Tab */
    [0x29] = 0x2C, /* Space */
    [0x4E] = 0x2D, /* - */
    [0x55] = 0x2E, /* = */
    [0x54] = 0x2F, /* [ */
    [0x5B] = 0x30, /* ] */
    [0x5D] = 0x31, /* \ */
    [0x4C] = 0x33, /* ; */
    [0x52] = 0x34, /* ' */
    [0x0E] = 0x35, /* ` */
    [0x41] = 0x36, /* , */
    [0x49] = 0x37, /* . */
    [0x4A] = 0x38, /* / */
    [0x58] = 0x39, /* Caps Lock */
    [0x05] = 0x3A, /* F1 */
    [0x06] = 0x3B, /* F2 */
    [0x04] = 0x3C, /* F3 */
    [0x0C] = 0x3D, /* F4 */
    [0x03] = 0x3E, /* F5 */
    [0x0B] = 0x3F, /* F6 */
    [0x83] = 0x40, /* F7 */
    [0x0A] = 0x41, /* F8 */
    [0x01] = 0x42, /* F9 */
    [0x09] = 0x43, /* F10 */
    [0x78] = 0x44, /* F11 */
    [0x07] = 0x45, /* F12 */
    [0x7E] = 0x47, /* Scroll Lock */
    [0x77] = 0x53, /* Num Lock */
    [0x14] = 0xE0, /* Left Ctrl */
    [0x12] = 0xE1, /* Left Shift */
    [0x11] = 0xE2, /* Left Alt */
    [0x59] = 0xE5, /* Right Shift */
};

/* Extended scancodes (preceded by 0xE0) */
static const uint8_t ps2_ext_to_hid[256] = {
    [0x75] = 0x52, /* Up */
    [0x72] = 0x51, /* Down */
    [0x6B] = 0x50, /* Left */
    [0x74] = 0x4F, /* Right */
    [0x70] = 0x49, /* Insert */
    [0x71] = 0x4C, /* Delete */
    [0x6C] = 0x4A, /* Home */
    [0x69] = 0x4D, /* End */
    [0x7D] = 0x4B, /* Page Up */
    [0x7A] = 0x4E, /* Page Down */
    [0x14] = 0xE4, /* Right Ctrl */
    [0x11] = 0xE6, /* Right Alt */
};

static bool ps2_extended = false;
static bool ps2_release = false;

void keyboard_ps2_init(void) {
    ps2_init(pio0, PS2_PIN_CLK, PS2_MOUSE_CLK);
    ps2_extended = false;
    ps2_release = false;
}

void keyboard_ps2_task(void) {
    while (ps2_kbd_has_data()) {
        int byte = ps2_kbd_get_byte();
        if (byte < 0)
            continue; /* frame error — skip */

        uint8_t sc = (uint8_t)byte;

        /* Keyboard self-test and ID responses - ignore */
        if (sc == 0xAA || sc == 0xFC || sc == 0xFE || sc == 0xFF)
            continue;

        if (sc == 0xE0) {
            ps2_extended = true;
            continue;
        }
        if (sc == 0xE1) {
            /* Pause/Break prefix - skip */
            continue;
        }
        if (sc == 0xF0) {
            ps2_release = true;
            continue;
        }

        /* Translate scancode to HID keycode */
        uint8_t hid_code;
        if (ps2_extended) {
            hid_code = ps2_ext_to_hid[sc];
            ps2_extended = false;
        } else {
            hid_code = ps2_to_hid[sc];
        }

        if (hid_code != 0) {
            keyboard_key_change(hid_code, !ps2_release);
        }
        ps2_release = false;
    }
}

void keyboard_ps2_set_led_status(uint8_t leds) {
    /* TODO: Send LED status command to PS/2 keyboard
     * Requires host-to-device PS/2 communication */
    (void)leds;
}

void keyboard_ps2_apply_settings(void) {
}
