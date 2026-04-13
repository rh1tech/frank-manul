/*
 * Manul - Serial Interface (Netcard via PIO UART)
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * ESP-01 wiring (pins from ESP-01's perspective):
 *   ESP-01 RX <- GPIO21 (our TX)
 *   ESP-01 TX -> GPIO20 (our RX)
 *
 * Hardware UART1 can't do this (GPIO20=TX, GPIO21=RX is the opposite),
 * so we use PIO-based UART on PIO1 (SM2 for TX, SM3 for RX).
 *
 * Uses PIO RX interrupt to drain into a 2KB ring buffer so we never
 * lose bytes even when the main loop is busy (screen redraw, printf).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "board_config.h"
#include "serial.h"
#include "uart_tx.pio.h"
#include "uart_rx.pio.h"

/* PIO UART on PIO1 (PIO0 is used by PS/2, PIO1 SM0 by I2S) */
#define SERIAL_PIO      pio1

/* ESP-01 pins: our TX = GPIO21 (to ESP RX), our RX = GPIO20 (from ESP TX) */
#define PIN_TX          21
#define PIN_RX          20

#define SERIAL_BAUD     115200

/* Interrupt-driven RX ring buffer (must be power of 2) */
#define RX_BUF_SIZE     2048
#define RX_BUF_MASK     (RX_BUF_SIZE - 1)

static uint tx_offset, rx_offset;
static uint tx_sm, rx_sm;

static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head;   /* written by ISR */
static volatile uint16_t rx_tail;   /* read by main loop */

static inline uint8_t pio_uart_read_byte_raw(void) {
    return (uint8_t)(pio_sm_get(SERIAL_PIO, rx_sm) >> 24);
}

/* PIO1 IRQ handler — drains PIO RX FIFO into the ring buffer */
static void pio1_rx_irq_handler(void) {
    while (!pio_sm_is_rx_fifo_empty(SERIAL_PIO, rx_sm)) {
        uint8_t c = pio_uart_read_byte_raw();
        uint16_t next_head = (rx_head + 1) & RX_BUF_MASK;
        if (next_head != rx_tail) {     /* drop byte if buffer full */
            rx_buf[rx_head] = c;
            rx_head = next_head;
        }
    }
}

void serial_init(void) {
    tx_sm = pio_claim_unused_sm(SERIAL_PIO, true);
    rx_sm = pio_claim_unused_sm(SERIAL_PIO, true);

    tx_offset = pio_add_program(SERIAL_PIO, &uart_tx_program);
    rx_offset = pio_add_program(SERIAL_PIO, &uart_rx_program);

    printf("PIO UART: TX sm=%u pin=%d, RX sm=%u pin=%d, baud=%u\n",
           (unsigned)tx_sm, PIN_TX,
           (unsigned)rx_sm, PIN_RX,
           SERIAL_BAUD);

    uart_tx_program_init(SERIAL_PIO, tx_sm, tx_offset, PIN_TX, SERIAL_BAUD);
    uart_rx_program_init(SERIAL_PIO, rx_sm, rx_offset, PIN_RX, SERIAL_BAUD);

    /* Set up interrupt: fire when RX FIFO is not empty */
    rx_head = 0;
    rx_tail = 0;

    uint pio_irq = PIO1_IRQ_0;
    /* Enable RXFIFO not-empty interrupt for our RX state machine */
    pio_set_irqn_source_enabled(SERIAL_PIO, 0, pis_sm0_rx_fifo_not_empty + rx_sm, true);
    irq_set_exclusive_handler(pio_irq, pio1_rx_irq_handler);
    irq_set_enabled(pio_irq, true);

    printf("PIO UART: ready (IRQ-buffered, %u byte RX buf)\n", RX_BUF_SIZE);
}

void serial_send_char(char c) {
    pio_sm_put_blocking(SERIAL_PIO, tx_sm, (uint32_t)c);
}

void serial_send_string(const char *s) {
    while (*s)
        pio_sm_put_blocking(SERIAL_PIO, tx_sm, (uint32_t)*s++);
}

void serial_send_data(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++)
        pio_sm_put_blocking(SERIAL_PIO, tx_sm, (uint32_t)data[i]);
}

bool serial_readable(void) {
    return rx_head != rx_tail;
}

uint8_t serial_read_byte(void) {
    while (rx_head == rx_tail)
        tight_loop_contents();
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) & RX_BUF_MASK;
    return c;
}
