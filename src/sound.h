/*
 * Iris 2350 - I2S Sound (beeper replacement)
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>
#include <stdbool.h>

void sound_play_tone(uint16_t frequency, uint16_t duration_ms, uint8_t volume, bool wait);
bool sound_playing(void);
void sound_init(void);

#endif
