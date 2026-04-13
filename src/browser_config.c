/*
 * Manul - Browser Configuration
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <stdio.h>
#include "browser_config.h"
#include "flash.h"
#include "font.h"

#define CONFIG_FLASH_SECTOR 0

static browser_config_t cfg;

/* Identity key mapping (no remapping) */
static uint8_t key_user_mapping[256];

/* Macro buffer (empty — first entry is 0 = end marker) */
static uint16_t macro_buffer[4] = {0};

void browser_config_init(void) {
    flash_read(CONFIG_FLASH_SECTOR, &cfg, sizeof(cfg));

    if (cfg.magic != BCFG_MAGIC) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic = BCFG_MAGIC;
        cfg.font_id = FONT_ID_WIN1251;
        cfg.keyboard_layout = 0;  /* US English */
        strncpy(cfg.homepage, "http://example.com", sizeof(cfg.homepage) - 1);
    }

    /* Initialize identity key mapping */
    for (int i = 0; i < 256; i++)
        key_user_mapping[i] = i;
}

void browser_config_save(void) {
    cfg.magic = BCFG_MAGIC;
    flash_write(CONFIG_FLASH_SECTOR, &cfg, sizeof(cfg));
}

const browser_config_t *browser_config_get(void) {
    return &cfg;
}

bool browser_config_has_wifi(void) {
    return cfg.wifi_ssid[0] != '\0';
}

void browser_config_set_wifi(const char *ssid, const char *pass) {
    strncpy(cfg.wifi_ssid, ssid, sizeof(cfg.wifi_ssid) - 1);
    cfg.wifi_ssid[sizeof(cfg.wifi_ssid) - 1] = '\0';
    strncpy(cfg.wifi_pass, pass, sizeof(cfg.wifi_pass) - 1);
    cfg.wifi_pass[sizeof(cfg.wifi_pass) - 1] = '\0';
    browser_config_save();
}

void browser_config_set_homepage(const char *url) {
    strncpy(cfg.homepage, url, sizeof(cfg.homepage) - 1);
    cfg.homepage[sizeof(cfg.homepage) - 1] = '\0';
    browser_config_save();
}

/* ---- Hardcoded config values for modules ---- */

uint8_t  config_get_screen_rows(void)          { return 30; }
uint8_t  config_get_screen_cols(void)          { return 80; }
bool     config_get_screen_dblchars(void)      { return false; }
uint8_t  config_get_screen_font_normal(void)   { return FONT_ID_WIN1251; }
uint8_t  config_get_screen_font_bold(void)     { return FONT_ID_TERMBOLD; }
uint8_t  config_get_screen_blink_period(void)  { return 0; }  /* disabled — no blinking in browser */

uint8_t  config_get_terminal_default_fg(void)  { return 7; }
uint8_t  config_get_terminal_default_bg(void)  { return 0; }
uint8_t  config_get_terminal_default_attr(void){ return 0; }

uint32_t config_get_serial_baud(void)          { return 115200; }

uint8_t  config_get_keyboard_layout(void)      { return cfg.keyboard_layout; }
bool     config_get_keyboard_scroll_lock(void) { return false; }
uint8_t  config_get_audible_bell_volume(void)  { return 50; }
uint8_t  config_get_input_method_toggle(void)  { return 0xFF; } /* disabled */
uint8_t *config_get_keyboard_user_mapping(void){ return key_user_mapping; }
uint16_t *config_get_keyboard_macros_start(void){ return macro_buffer; }

bool     config_menu_active(void)              { return false; }
