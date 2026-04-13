/*
 * Iris 2350
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://rh1.tech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "hardware/vreg.h"

/*
 * Board Configuration for Iris 2350
 *
 * M2-only build.
 *
 * M2 GPIO Layout:
 *   PS/2 Mouse:    CLK=0, DATA=1
 *   PS/2 Kbd:      CLK=2, DATA=3
 *   SD Card:       MISO=4, CSn=5, SCK=6, MOSI=7
 *   PSRAM:         CS=8 (RP2350A) or CS=47 (RP2350B)
 *   I2S Audio:     DATA=9, BCLK=10, LRCLK=11
 *   HDMI (HSTX):   CLK-=12, CLK+=13, D0-=14, D0+=15, D1-=16, D1+=17, D2-=18, D2+=19
 *   ESP-01 UART1:  TX=20 (-> ESP RX), RX=21 (<- ESP TX)
 *   LED:           25
 */

#define BOARD_M2

//=============================================================================
// CPU Speed
//=============================================================================
#ifndef CPU_CLOCK_MHZ
#define CPU_CLOCK_MHZ 252
#endif

#ifndef CPU_VOLTAGE
#define CPU_VOLTAGE VREG_VOLTAGE_1_15
#endif

//=============================================================================
// PS/2 Keyboard (PIO)
//=============================================================================
#define PS2_PIN_CLK  2
#define PS2_PIN_DATA 3

//=============================================================================
// PS/2 Mouse (PIO)
//=============================================================================
#define PS2_MOUSE_CLK  0
#define PS2_MOUSE_DATA 1

//=============================================================================
// I2S Audio (PIO1, GPIO 9/10/11)
//=============================================================================
#define I2S_DATA_PIN       9
#define I2S_CLOCK_PIN_BASE 10   /* BCLK=10, LRCLK=11 */

//=============================================================================
// ESP-01 Serial (UART1, GPIO 20/21)
//=============================================================================
#define ESP_UART_ID   uart1
#define ESP_UART_TX   20   /* UART1 TX -> ESP-01 RX */
#define ESP_UART_RX   21   /* UART1 RX <- ESP-01 TX */

//=============================================================================
// On-board LED
//=============================================================================
#define PIN_LED 25

#endif // BOARD_CONFIG_H
