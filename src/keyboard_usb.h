/*
 * Iris 2350 - USB HID Keyboard (via frank-wolf usbhid driver)
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KEYBOARD_USB_H
#define KEYBOARD_USB_H

#include <stdint.h>

void keyboard_usb_set_led_status(uint8_t leds);
void keyboard_usb_init(void);
void keyboard_usb_task(void);
void keyboard_usb_apply_settings(void);

#endif
