/*
 * Manul - Serial Interface (Netcard via PIO UART)
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>
#include <stdint.h>

void serial_init(void);
void serial_send_char(char c);
void serial_send_string(const char *s);
void serial_send_data(const uint8_t *data, uint16_t len);
bool serial_readable(void);
uint8_t serial_read_byte(void);

#endif
