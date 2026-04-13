/*
 * Manul - WiFi Configuration UI
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include <stdint.h>
#include <stdbool.h>

/* Enter WiFi setup mode (called from browser on F2) */
void wifi_setup_enter(void);

/* Process keyboard input in WiFi setup mode */
void wifi_setup_process_key(uint16_t key);

/* Check if WiFi setup is currently active */
bool wifi_setup_active(void);

#endif
