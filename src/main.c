/*
 * Manul - Text Web Browser for RP2350
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * Lynx-inspired minimal text web browser for the RP2350 M2 platform.
 * Uses HSTX HDMI output, PS/2 and USB keyboards, and ESP-01 WiFi
 * via frank-netcard AT command firmware.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"

#include "framebuf.h"
#include "serial.h"
#include "keyboard.h"
#include "browser_config.h"
#include "font.h"
#include "sound.h"
#include "board_config.h"
#include "netcard.h"
#include "http.h"
#include "browser.h"
#include "psram_init.h"
#include "psram_allocator.h"

#include "tusb.h"

#define BOOTSEL_TIMEOUT_MS 1500
static absolute_time_t bootsel_timeout = 0;
static const uint32_t bootsel_magic[] = {0xf01681de, 0xbd729b29, 0xd359be7a};
static uint32_t __uninitialized_ram(bootsel_magic_ram)[count_of(bootsel_magic)];

void wait(uint32_t milliseconds);

void run_tasks(bool processInput) {
#ifdef USB_HID
    if (tuh_inited())
        tuh_task();
#endif

    /* Drain serial FIRST */
    netcard_poll();

    /* Process deferred HTTP redirects (must be outside netcard_poll context) */
    http_poll();

    /* Cursor and text blink — this redraws the full screen and can
     * take several ms.  Drain serial again afterwards. */
    framebuf_blink_task();
    netcard_poll();

    /* Handle bootsel mechanism timeout */
    if (bootsel_timeout > 0 && get_absolute_time() >= bootsel_timeout) {
        bootsel_timeout = 0;
        for (uint i = 0; i < count_of(bootsel_magic); i++)
            bootsel_magic_ram[i] = 0;
    }

    /* Process keyboard input */
    keyboard_task();
    netcard_poll();
    if (processInput && keyboard_num_keypress() > 0) {
        uint16_t raw_key = keyboard_read_keypress();
        uint8_t modifiers = raw_key >> 8;
        uint8_t ascii = keyboard_map_key_ascii(raw_key, NULL);
        if (ascii != 0) {
            /* Pack mapped ASCII + modifiers so browser can check Shift/Ctrl */
            uint16_t key = ascii | ((uint16_t)modifiers << 8);
            browser_process_key(key);
        }
    }
}

void wait(uint32_t milliseconds) {
    absolute_time_t timeout = make_timeout_time_ms(milliseconds);
    while (get_absolute_time() < timeout)
        run_tasks(false);
}

int main(void) {
    /* Set CPU voltage and clock */
    vreg_set_voltage(CPU_VOLTAGE);
    sleep_ms(10);
    set_sys_clock_khz(CPU_CLOCK_MHZ * 1000, true);

    /* LED init */
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, true);
    gpio_put(PIN_LED, 1);

    /* Bootsel double-tap mechanism */
    uint i;
    for (i = 0; i < count_of(bootsel_magic) && bootsel_magic_ram[i] == bootsel_magic[i]; i++)
        ;

    if (i < count_of(bootsel_magic)) {
        for (i = 0; i < count_of(bootsel_magic); i++)
            bootsel_magic_ram[i] = bootsel_magic[i];
        bootsel_timeout = make_timeout_time_ms(BOOTSEL_TIMEOUT_MS);
    } else {
        for (i = 0; i < count_of(bootsel_magic); i++)
            bootsel_magic_ram[i] = 0;
        reset_usb_boot(1 << PIN_LED, 0);
    }

    /* Initialize configuration */
    browser_config_init();

#ifdef USB_HID
    /* USB Host mode for HID keyboard */
    tuh_init(BOARD_TUH_RHPORT);
#else
    /* USB Device mode for CDC serial console (with 5 second delay) */
    stdio_init_all();
    sleep_ms(5000);
    printf("Manul v%s\n", FRANKMANUL_VERSION_STR);
#endif

    /* PSRAM init (available but not used for render buffer yet —
     * XIP cache doesn't write back to PSRAM on eviction on this board.
     * IRQ handlers running flash code evict PSRAM cache lines.
     * Needs QMI direct-mode write or DMA solution.) */
    psram_init(47);

    /* Initialize serial (netcard via PIO UART) — after stdio so debug prints work */
    serial_init();

    /* Initialize keyboard (PS/2 always, USB HID when enabled) */
    keyboard_init();

    /* Allow time for keyboard(s) to initialize */
#ifdef USB_HID
    wait(1500);
#else
    wait(250);
#endif

    /* Initialize display (HSTX HDMI 640x480x16) and framebuffer */
    framebuf_init();

    /* Initialize I2S sound */
    sound_init();

    /* Initialize netcard (wait for ESP-01 +READY) */
    netcard_init();

    /* Initialize browser UI */
    browser_init();

    /* Main loop */
    while (true)
        run_tasks(true);
}
