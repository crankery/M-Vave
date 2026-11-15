/* SPDX-License-Identifier: GPL-3.0-only */
/* Dry, integer-only DMX player: eight monophonic voice-card channels. */
#ifndef DMX_PLAYER_H
#define DMX_PLAYER_H

#include <stdint.h>
#include "dmx_decode_table.h"
#include "dmx_roms.h"

#define DMX_CHANNEL_COUNT 8u
#define DMX_PAD_COUNT 24u
/* 24 kHz source / 44.1 kHz output, Q16. */
#define DMX_PHASE_STEP 35666u
/* Four cascaded one-pole low-pass stages approximate the tom card's 3320.
 * The coefficient follows a short trigger decay and scales with sample rate. */
#define DMX_TOM_FILTER_LOW 6000u
#define DMX_TOM_FILTER_HIGH 22000u

typedef struct {
    uint32_t step;
    uint16_t start, length;
    uint8_t rom, level;
} dmx_pad_t;

/* Three triggers per channel. The stock percussion ROMs each hold two 2 KiB
 * sounds: STIK has rimshot then tambourine; SHAKE6 has clap then shaker.
 * Rates and trigger levels are audition settings, not measured card values. */
#define PAD(step, rom, start, length, level) {step, start, length, rom, level}
static const dmx_pad_t dmx_pads[DMX_PAD_COUNT] = {
    PAD(DMX_PHASE_STEP, DMX_ROM_KICK, 0, 4096, 64), PAD(DMX_PHASE_STEP, DMX_ROM_KICK, 0, 4096, 96), PAD(DMX_PHASE_STEP, DMX_ROM_KICK, 0, 4096, 127),
    PAD(DMX_PHASE_STEP, DMX_ROM_SNARE, 0, 4096, 64), PAD(DMX_PHASE_STEP, DMX_ROM_SNARE, 0, 4096, 96), PAD(DMX_PHASE_STEP, DMX_ROM_SNARE, 0, 4096, 127),
    PAD(DMX_PHASE_STEP, DMX_ROM_HAT, 0, 4096, 80), PAD(DMX_PHASE_STEP, DMX_ROM_HAT, 0, 4096, 127), PAD(DMX_PHASE_STEP, DMX_ROM_HAT, 0, 4096, 100),
    PAD(43000u, DMX_ROM_SMALL_TOM, 0, 4096, 127), PAD(DMX_PHASE_STEP, DMX_ROM_SMALL_TOM, 0, 4096, 127), PAD(30000u, DMX_ROM_SMALL_TOM, 0, 4096, 127),
    PAD(43000u, DMX_ROM_LARGE_TOM, 0, 4096, 127), PAD(DMX_PHASE_STEP, DMX_ROM_LARGE_TOM, 0, 4096, 127), PAD(30000u, DMX_ROM_LARGE_TOM, 0, 4096, 127),
    PAD(DMX_PHASE_STEP, DMX_ROM_RIDE, 0, 16384, 80), PAD(DMX_PHASE_STEP, DMX_ROM_RIDE, 0, 16384, 127), PAD(DMX_PHASE_STEP, DMX_ROM_CRASH, 0, 16384, 127),
    PAD(DMX_PHASE_STEP, DMX_ROM_PERC1, 2048, 2048, 90), PAD(DMX_PHASE_STEP, DMX_ROM_PERC1, 2048, 2048, 127), PAD(DMX_PHASE_STEP, DMX_ROM_PERC1, 0, 2048, 127),
    PAD(DMX_PHASE_STEP, DMX_ROM_PERC2, 2048, 2048, 80), PAD(DMX_PHASE_STEP, DMX_ROM_PERC2, 2048, 2048, 127), PAD(DMX_PHASE_STEP, DMX_ROM_PERC2, 0, 2048, 127),
};
#undef PAD
static const uint8_t dmx_has_alt[DMX_CHANNEL_COUNT] = {1, 1, 1, 1, 1, 0, 0, 1};

static dmx_pad_t dmx_pad_for(uint32_t pad, uint8_t alt)
{
    dmx_pad_t spec = dmx_pads[pad];
    if (!alt || !dmx_has_alt[pad / 3u]) return spec;
    switch (pad / 3u) {
    case 0: spec.rom = DMX_ROM_DX_KICK; spec.length = 8192u; break;
    case 1: spec.rom = DMX_ROM_DX_SNARE; spec.length = 8192u; break;
    case 2: spec.rom = DMX_ROM_DX_HAT; spec.length = 8192u; break;
    case 3:
    case 4: spec.rom = DMX_ROM_DX_TOM; spec.length = 8192u; break;
    case 7:
        spec.rom = DMX_ROM_DX_PERC2;
        spec.start = pad == 23u ? 0u : 4096u;
        spec.length = 4096u;
        break;
    }
    return spec;
}

typedef struct {
    uint32_t seen, phase, step;
    uint16_t start, length;
    uint8_t pad, rom, gain;
    uint16_t filter_env, filter_low, filter_span;
    int32_t filter_stage[4];
    volatile uint8_t active;
} dmx_voice_t;

typedef struct {
    /* Main loop publishes each channel's request in one atomic 32-bit store. */
    volatile uint32_t request[DMX_CHANNEL_COUNT];
    volatile uint8_t level[DMX_CHANNEL_COUNT];
    volatile uint8_t source[DMX_CHANNEL_COUNT]; /* 0 stock DMX, 1 DX alternative */
    /* Audio callback alone writes voices. */
    dmx_voice_t voice[DMX_CHANNEL_COUNT];
} dmx_player_t;

static void dmx_init(dmx_player_t *p)
{
    uint32_t ch;
    for (ch = 0; ch < DMX_CHANNEL_COUNT; ch++) p->level[ch] = 100u;
}

static void dmx_trigger(dmx_player_t *p, uint32_t pad, uint8_t velocity)
{
    uint32_t ch, old;
    dmx_pad_t spec;
    if (pad >= DMX_PAD_COUNT || !velocity) return;
    ch = pad / 3u;
    spec = dmx_pad_for(pad, p->source[ch]);
    if ((uint32_t)spec.start + spec.length > dmx_rom_length[spec.rom]) return;
    old = p->request[ch];
    p->request[ch] = (((old >> 16) + 1u) << 16) |
        ((uint32_t)(p->source[ch] != 0) << 15) | ((uint32_t)velocity << 8) | pad;
}

static void dmx_render(dmx_player_t *p, int32_t *out, uint32_t frames)
{
    uint32_t ch, i;
    for (ch = 0; ch < DMX_CHANNEL_COUNT; ch++) {
        dmx_voice_t *v = &p->voice[ch];
        uint32_t request = p->request[ch], phase;
        uint8_t level = p->level[ch];
        if (request != v->seen) {
            uint32_t pad = request & 255u;
            dmx_pad_t spec = dmx_pad_for(pad, (uint8_t)((request >> 15) & 1u));
            v->seen = request;
            v->pad = (uint8_t)pad;
            v->rom = spec.rom;
            v->step = spec.step;
            v->start = spec.start;
            v->length = spec.length;
            v->phase = 0;
            v->gain = (uint8_t)((((request >> 8) & 127u) * spec.level) / 127u);
            if (ch == 3u || ch == 4u) {
                uint32_t pole;
                v->filter_low = (uint16_t)(DMX_TOM_FILTER_LOW * spec.step / DMX_PHASE_STEP);
                v->filter_span = (uint16_t)((DMX_TOM_FILTER_HIGH - DMX_TOM_FILTER_LOW) * spec.step / DMX_PHASE_STEP);
                v->filter_env = 32767u;
                for (pole = 0; pole < 4u; pole++) v->filter_stage[pole] = 0;
            }
            v->active = 1;
        }
        if (!v->active) continue;
        phase = v->phase;
        for (i = 0; i < frames; i++) {
            uint32_t pos = phase >> 16;
            int32_t sample;
            if (pos >= v->length) { v->active = 0; break; }
            sample = dmx_decode_table[dmx_rom_data[dmx_rom_offset[v->rom] + v->start + pos]];
            if (ch == 3u || ch == 4u) {
                uint32_t pole, alpha = v->filter_low +
                    ((uint32_t)v->filter_span * v->filter_env >> 15);
                for (pole = 0; pole < 4u; pole++) {
                    int32_t state = v->filter_stage[pole];
                    state += ((sample - state) * (int32_t)alpha) >> 15;
                    v->filter_stage[pole] = state;
                    sample = state;
                }
                if (v->filter_env)
                    v->filter_env -= (uint16_t)((v->filter_env + 4095u) >> 12);
            }
            /* At most eight channels; even full-scale samples cannot clip the sum. */
            sample = (sample * v->gain * level) / (127 * 127 * 8);
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
            phase += v->step;
        }
        v->phase = phase;
    }
}

#endif
