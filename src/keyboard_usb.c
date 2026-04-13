/*
 * Iris 2350 - USB HID Keyboard (via frank-wolf usbhid driver)
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * Wraps the frank-wolf usbhid driver to provide keyboard input
 * via the keyboard_key_change() interface expected by keyboard.c.
 *
 * Only compiled when USB_HID is defined.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifdef USB_HID

#include "keyboard_usb.h"
#include "keyboard.h"
#include "usbhid.h"
#include "iris_config.h"
#include "pico/time.h"

static uint8_t usb_repeat_key = 0;
static absolute_time_t usb_repeat_timeout = 0;

void keyboard_usb_set_led_status(uint8_t leds) {
    (void)leds;
    /* TODO: USB HID LED control not yet supported by usbhid driver */
}

void keyboard_usb_init(void) {
    usbhid_init();
}

void keyboard_usb_task(void) {
    usbhid_task();

    /* Drain USB keyboard events and feed to keyboard_key_change() */
    uint8_t keycode;
    int down;
    while (usbhid_get_key_action(&keycode, &down)) {
        keyboard_key_change(keycode, down != 0);

        if (down) {
            usb_repeat_key = keycode;
            usb_repeat_timeout = make_timeout_time_ms(config_get_keyboard_repeat_delay_ms());
        } else if (keycode == usb_repeat_key) {
            usb_repeat_key = HID_KEY_NONE;
        }
    }

    /* Key repeat */
    if (usb_repeat_key != HID_KEY_NONE && get_absolute_time() >= usb_repeat_timeout) {
        keyboard_key_change(usb_repeat_key, true);
        usb_repeat_timeout = make_timeout_time_ms(1000000 / config_get_keyboard_repeat_rate_mHz());
    }
}

void keyboard_usb_apply_settings(void) {
}

#else /* !USB_HID */

/* Stubs when USB HID is disabled */
#include "keyboard_usb.h"

void keyboard_usb_set_led_status(uint8_t leds) { (void)leds; }
void keyboard_usb_init(void) {}
void keyboard_usb_task(void) {}
void keyboard_usb_apply_settings(void) {}

#endif /* USB_HID */
