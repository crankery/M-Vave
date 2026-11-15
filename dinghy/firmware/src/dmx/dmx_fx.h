/* SPDX-License-Identifier: GPL-3.0-only */
/* Small dry/wet stereo echo and bus compressor for the FM-1 output. */
#ifndef DMX_FX_H
#define DMX_FX_H

#include <stdint.h>

#define DMX_ECHO_LEFT 8192u   /* 186 ms at 44.1 kHz */
#define DMX_ECHO_RIGHT 12288u /* 279 ms */
#define DMX_COMP_THRESHOLD 3000u

#ifdef DINGHY_HOST
#define DMX_FX_STORAGE
#else
#define DMX_FX_STORAGE __attribute__((section(".pool")))
#endif
static int16_t dmx_echo_left[DMX_ECHO_LEFT] DMX_FX_STORAGE;
static int16_t dmx_echo_right[DMX_ECHO_RIGHT] DMX_FX_STORAGE;
#undef DMX_FX_STORAGE

typedef struct {
    volatile uint8_t wet, tail; /* 0..127, changed by the main loop */
    uint32_t left_pos, right_pos;
    int32_t damp_left, damp_right;
    int32_t comp_gain_q15;
} dmx_fx_t;

static int16_t dmx_fx_clip16(int32_t x)
{
    if (x > 32767) return 32767;
    if (x < -32767) return -32767;
    return (int16_t)x;
}

static void dmx_fx_init(dmx_fx_t *fx)
{
    fx->wet = 40u;
    fx->tail = 70u;
    fx->comp_gain_q15 = 32768;
}

static void dmx_fx_render(dmx_fx_t *fx, int32_t *out, uint32_t frames)
{
    uint32_t i, peak = 0;
    uint8_t wet = fx->wet, tail = fx->tail;
    int32_t gain, target;

    /* Two different delay lengths and crossed, damped feedback make a short
     * stereo echo. Writing the dry signal keeps the tail ready when wet is 0. */
    for (i = 0; i < frames; i++) {
        int32_t dry = (out[2u * i] + out[2u * i + 1u]) / 2;
        int32_t left = dmx_echo_left[fx->left_pos];
        int32_t right = dmx_echo_right[fx->right_pos];
        int32_t value, magnitude;
        fx->damp_left += (left - fx->damp_left) >> 2;
        fx->damp_right += (right - fx->damp_right) >> 2;
        dmx_echo_left[fx->left_pos] = dmx_fx_clip16(dry / 2 + (fx->damp_right * tail * 3) / 512);
        dmx_echo_right[fx->right_pos] = dmx_fx_clip16(dry / 2 + (fx->damp_left * tail * 3) / 512);
        if (++fx->left_pos == DMX_ECHO_LEFT) fx->left_pos = 0;
        if (++fx->right_pos == DMX_ECHO_RIGHT) fx->right_pos = 0;
        out[2u * i] += left * wet / 128;
        out[2u * i + 1u] += right * wet / 128;
        value = out[2u * i];
        magnitude = value < 0 ? -value : value;
        if ((uint32_t)magnitude > peak) peak = (uint32_t)magnitude;
        value = out[2u * i + 1u];
        magnitude = value < 0 ? -value : value;
        if ((uint32_t)magnitude > peak) peak = (uint32_t)magnitude;
    }

    /* One gain target per audio block, with fast attack and slow release.
     * Above threshold, a 3:1 slope reduces peaks without a hard waveform cut. */
    target = 32768;
    if (peak > DMX_COMP_THRESHOLD)
        target = (int32_t)((DMX_COMP_THRESHOLD + (peak - DMX_COMP_THRESHOLD) / 3u) * 32768u / peak);
    gain = fx->comp_gain_q15;
    for (i = 0; i < frames; i++) {
        int32_t left, right;
        if (target < gain) gain += (target - gain) / 4;
        else gain += (target - gain) / 256;
        left = out[2u * i] * gain / 32768;
        right = out[2u * i + 1u] * gain / 32768;
        out[2u * i] = dmx_fx_clip16(left);
        out[2u * i + 1u] = dmx_fx_clip16(right);
    }
    fx->comp_gain_q15 = gain;
}

#endif
