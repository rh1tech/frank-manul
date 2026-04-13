/*
 * Iris 2350 - I2S Sound (beeper replacement)
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * Generates square-wave beep tones via the I2S audio system.
 * Replaces the hardware PWM buzzer from the original Iris.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "audio.h"
#include "board_config.h"
#include "sound.h"

// defined in main.c
void run_tasks(bool processInput);

#define BEEP_SAMPLE_RATE  22050
#define BEEP_BUFFER_SIZE  512       /* stereo frames per DMA buffer */
#define SQUARE_WAVE_AMP   0x2000

static i2s_config_t audio_cfg;
static volatile bool tone_active = false;
static volatile uint16_t tone_freq = 0;
static volatile uint32_t tone_samples_remaining = 0;
static volatile uint8_t  tone_volume = 50;
static volatile uint32_t tone_phase = 0;

static void beep_fill_callback(int buf_index, uint32_t *buf, uint32_t frames) {
    (void)buf_index;
    int16_t *samples = (int16_t *)buf;

    if (!tone_active || tone_freq == 0) {
        memset(buf, 0, frames * sizeof(uint32_t));
        return;
    }

    uint16_t freq = tone_freq;
    uint32_t phase = tone_phase;
    uint32_t remaining = tone_samples_remaining;
    int16_t amp = (int16_t)((SQUARE_WAVE_AMP * tone_volume) / 100);

    for (uint32_t i = 0; i < frames; i++) {
        if (remaining == 0) {
            samples[i * 2]     = 0;    // left
            samples[i * 2 + 1] = 0;    // right
            continue;
        }

        /* Square wave: toggle based on half-period */
        uint32_t half_period = BEEP_SAMPLE_RATE / (freq * 2);
        if (half_period == 0) half_period = 1;
        int16_t val = ((phase / half_period) & 1) ? amp : -amp;

        samples[i * 2]     = val;   // left
        samples[i * 2 + 1] = val;   // right

        phase++;
        remaining--;
    }

    tone_phase = phase;
    tone_samples_remaining = remaining;

    if (remaining == 0) {
        tone_active = false;
    }
}

bool sound_playing(void) {
    return tone_active;
}

void sound_play_tone(uint16_t frequency, uint16_t duration_ms, uint8_t volume, bool wait) {
    if (volume == 0 || frequency == 0 || duration_ms == 0)
        return;

    tone_freq = frequency;
    tone_volume = volume > 100 ? 100 : volume;
    tone_phase = 0;
    tone_samples_remaining = (uint32_t)BEEP_SAMPLE_RATE * duration_ms / 1000;
    __dmb();
    tone_active = true;

    if (wait) {
        while (sound_playing())
            run_tasks(false);
    }
}

void sound_init(void) {
    memset(&audio_cfg, 0, sizeof(audio_cfg));
    audio_cfg.sample_freq    = BEEP_SAMPLE_RATE;
    audio_cfg.channel_count  = 2;
    audio_cfg.data_pin       = I2S_DATA_PIN;
    audio_cfg.clock_pin_base = I2S_CLOCK_PIN_BASE;
    audio_cfg.pio            = pio1;
    audio_cfg.dma_trans_count = BEEP_BUFFER_SIZE;
    audio_cfg.volume         = 0;   /* no extra attenuation */

    i2s_init(&audio_cfg);
    i2s_set_fill_callback(beep_fill_callback);
    i2s_start();
}
