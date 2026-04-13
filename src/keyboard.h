/*
 * Manul - Keyboard Interface
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * Based on Iris by Mikhail Matveev / VersaTerm by David Hansel
 *
 * Supports PS/2 keyboard (always) and USB HID keyboard (when USB_HID enabled).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "tusb.h"
#include "class/hid/hid.h"

/* Key values for control keys in ASCII range */
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0d
#define KEY_ESC         0x1b
#define KEY_DELETE      0x7f

/* Key values for special (non-ASCII) control keys */
#define KEY_UP          0x80
#define KEY_DOWN        0x81
#define KEY_LEFT        0x82
#define KEY_RIGHT       0x83
#define KEY_INSERT      0x84
#define KEY_HOME        0x85
#define KEY_END         0x86
#define KEY_PUP         0x87
#define KEY_PDOWN       0x88
#define KEY_PAUSE       0x89
#define KEY_PRSCRN      0x8a
#define KEY_F1          0x8c
#define KEY_F2          0x8d
#define KEY_F3          0x8e
#define KEY_F4          0x8f
#define KEY_F5          0x90
#define KEY_F6          0x91
#define KEY_F7          0x92
#define KEY_F8          0x93
#define KEY_F9          0x94
#define KEY_F10         0x95
#define KEY_F11         0x96
#define KEY_F12         0x97

/* Modifier flags */
#define KEYBOARD_MODIFIER_LEFTCTRL   0x01
#define KEYBOARD_MODIFIER_LEFTSHIFT  0x02
#define KEYBOARD_MODIFIER_LEFTALT    0x04
#define KEYBOARD_MODIFIER_RIGHTCTRL  0x10
#define KEYBOARD_MODIFIER_RIGHTSHIFT 0x20
#define KEYBOARD_MODIFIER_RIGHTALT   0x40
#define KEYBOARD_MODIFIER_LEFTGUI    0x08
#define KEYBOARD_MODIFIER_RIGHTGUI   0x80

/* LED flags */
#define KEYBOARD_LED_NUMLOCK    0x01
#define KEYBOARD_LED_CAPSLOCK   0x02
#define KEYBOARD_LED_SCROLLLOCK 0x04

void    keyboard_init(void);
void    keyboard_apply_settings(void);
void    keyboard_task(void);
void    keyboard_key_change(uint8_t key, bool make);

size_t   keyboard_num_keypress(void);
uint16_t keyboard_read_keypress(void);
uint8_t  keyboard_get_led_status(void);
uint8_t  keyboard_get_current_modifiers(void);
bool     keyboard_ctrl_pressed(uint16_t key);
bool     keyboard_alt_pressed(uint16_t key);
bool     keyboard_shift_pressed(uint16_t key);
uint8_t  keyboard_map_key_ascii(uint16_t key, bool *isaltcode);

#endif
