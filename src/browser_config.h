/*
 * Manul - Browser Configuration
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * Replaces iris_config.h — provides all config_get_*() functions
 * that other modules expect, with hardcoded or flash-stored values.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BROWSER_CONFIG_H
#define BROWSER_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Persistent config stored in flash ---- */
#define BCFG_MAGIC 0x464C5958  /* "FLYX" */

typedef struct {
    uint32_t magic;
    char     wifi_ssid[33];
    char     wifi_pass[65];
    char     homepage[256];
    uint8_t  font_id;
    uint8_t  keyboard_layout;  /* 0=US, 1=UK, 2=FR, 3=DE, 4=IT, 5=BE, 6=ES */
    uint8_t  reserved[64];
} browser_config_t;

void browser_config_init(void);
void browser_config_save(void);
const browser_config_t *browser_config_get(void);

bool browser_config_has_wifi(void);
void browser_config_set_wifi(const char *ssid, const char *pass);
void browser_config_set_homepage(const char *url);

/* ---- Functions expected by keyboard.c, framebuf.c, font.c, serial.c ---- */

/* Screen */
uint8_t  config_get_screen_rows(void);       /* 30 */
uint8_t  config_get_screen_cols(void);       /* 80 */
bool     config_get_screen_dblchars(void);   /* false */
uint8_t  config_get_screen_font_normal(void);
uint8_t  config_get_screen_font_bold(void);
uint8_t  config_get_screen_blink_period(void); /* 30 (~500ms) */

/* Terminal defaults (used by framebuf fill) */
uint8_t  config_get_terminal_default_fg(void);   /* 7 (light gray) */
uint8_t  config_get_terminal_default_bg(void);   /* 0 (black) */
uint8_t  config_get_terminal_default_attr(void);  /* 0 */

/* Serial */
uint32_t config_get_serial_baud(void);       /* 115200 */

/* Keyboard */
uint8_t  config_get_keyboard_layout(void);
bool     config_get_keyboard_scroll_lock(void);  /* false */
uint8_t  config_get_audible_bell_volume(void);   /* 50 */
uint8_t  config_get_input_method_toggle(void);   /* 0xFF = disabled */
uint8_t *config_get_keyboard_user_mapping(void);
uint16_t *config_get_keyboard_macros_start(void);

/* Config menu stub */
bool     config_menu_active(void);               /* false */

#endif
