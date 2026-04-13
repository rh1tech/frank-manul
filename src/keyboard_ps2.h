/*
 * Iris 2350 - PS/2 Keyboard (via frank-wolf ps2 driver)
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KEYBOARD_PS2_H
#define KEYBOARD_PS2_H

#include <stdint.h>

void keyboard_ps2_set_led_status(uint8_t leds);
void keyboard_ps2_task(void);
void keyboard_ps2_init(void);
void keyboard_ps2_apply_settings(void);

#endif
