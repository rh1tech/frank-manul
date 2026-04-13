/*
 * Manul - ESP-01 Netcard AT Command Client
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * AT command client for the frank-netcard ESP-01 firmware.
 * Communicates over PIO UART using serial.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NETCARD_H
#define NETCARD_H

#include <stdbool.h>
#include <stdint.h>

/* Async callback types */
typedef void (*nc_data_cb_t)(uint8_t socket_id, const uint8_t *data, uint16_t len);
typedef void (*nc_close_cb_t)(uint8_t socket_id);
typedef void (*nc_wifi_cb_t)(bool connected, const char *ip);
typedef void (*nc_scan_cb_t)(const char *ssid, int rssi, int enc, int ch);

/* Initialization and polling */
void netcard_init(void);
void netcard_poll(void);

/* WiFi */
bool netcard_wifi_join(const char *ssid, const char *pass);
void netcard_wifi_quit(void);
bool netcard_wifi_connected(void);

/* WiFi scan */
int  netcard_wifi_scan(nc_scan_cb_t cb);

/* Sockets (ids 0-3) */
bool netcard_socket_open(uint8_t id, bool tls, const char *host, uint16_t port);
bool netcard_socket_send(uint8_t id, const uint8_t *data, uint16_t len);
void netcard_socket_close(uint8_t id);

/* Async callbacks */
void netcard_set_data_callback(nc_data_cb_t cb);
void netcard_set_close_callback(nc_close_cb_t cb);
void netcard_set_wifi_callback(nc_wifi_cb_t cb);

#endif /* NETCARD_H */
