/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DUBSIREN_PLAYER_H
#define DUBSIREN_PLAYER_H

#include "dubsiren_samples.h"

#define SFX_VOICES 3u
#define SFX_PHASE_STEP 23777u /* 16 kHz source into 44.1 kHz output, Q16 */

static const int16_t adpcm_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
    41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157,
    173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
    1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
    13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};
static const int8_t adpcm_index[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
};

typedef struct {
    uint32_t position, phase, age;
    int32_t predictor, current, next;
    uint8_t sample, index, active, key, midi, gated;
} sfx_voice_t;

static sfx_voice_t sfx_voices[SFX_VOICES];
static uint32_t sfx_age;

static int32_t sfx_decode(sfx_voice_t *voice)
{
    const dubsiren_sample_info_t *info = &dubsiren_sample_info[voice->sample];
    uint8_t nibble;
    int32_t step, delta, value;
    if (voice->position >= info->frames) return 0;
    nibble = dubsiren_sample_data[info->offset + voice->position / 2u];
    if (voice->position & 1u) nibble >>= 4;
    else nibble &= 15u;
    voice->position++;
    step = adpcm_step[voice->index];
    delta = step >> 3;
    if (nibble & 4u) delta += step;
    if (nibble & 2u) delta += step >> 1;
    if (nibble & 1u) delta += step >> 2;
    value = voice->predictor + ((nibble & 8u) ? -delta : delta);
    if (value > 32767) value = 32767;
    if (value < -32768) value = -32768;
    voice->predictor = value;
    value = (int32_t)voice->index + adpcm_index[nibble];
    if (value < 0) value = 0;
    if (value > 88) value = 88;
    voice->index = (uint8_t)value;
    return voice->predictor;
}

static void sfx_fire(uint8_t sample, uint8_t key, uint8_t midi, uint8_t gated)
{
    uint32_t i, choice = 0;
    if (sample >= DUBSIREN_SAMPLE_COUNT) return;
#ifndef DINGHY_HOST
    fm1_irq_off(); /* publish a complete new voice before the audio ISR sees it */
#endif
    if (gated) {
        for (i = 0; i < SFX_VOICES; i++)
            if (sfx_voices[i].active && sfx_voices[i].gated &&
                sfx_voices[i].key == key && sfx_voices[i].midi == midi)
                sfx_voices[i].active = 0u;
    }
    for (i = 0; i < SFX_VOICES; i++) {
        if (!sfx_voices[i].active) { choice = i; break; }
        if (sfx_voices[i].age < sfx_voices[choice].age) choice = i;
    }
    sfx_voices[choice] = (sfx_voice_t){0};
    sfx_voices[choice].sample = sample;
    sfx_voices[choice].key = key;
    sfx_voices[choice].midi = midi;
    sfx_voices[choice].gated = gated;
    sfx_voices[choice].age = ++sfx_age;
    sfx_voices[choice].active = 1u;
    sfx_voices[choice].current = sfx_decode(&sfx_voices[choice]);
    sfx_voices[choice].next = sfx_decode(&sfx_voices[choice]);
#ifndef DINGHY_HOST
    fm1_irq_on();
#endif
}

static void sfx_release(uint8_t key, uint8_t midi)
{
    uint32_t i;
#ifndef DINGHY_HOST
    fm1_irq_off();
#endif
    for (i = 0; i < SFX_VOICES; i++)
        if (sfx_voices[i].active && sfx_voices[i].gated &&
            sfx_voices[i].key == key && sfx_voices[i].midi == midi)
            sfx_voices[i].active = 0u;
#ifndef DINGHY_HOST
    fm1_irq_on();
#endif
}

static void sfx_render(int32_t *out, uint32_t frames)
{
    uint32_t i, v;
    for (i = 0; i < frames; i++) {
        int32_t mixed = 0;
        for (v = 0; v < SFX_VOICES; v++) {
            sfx_voice_t *voice = &sfx_voices[v];
            if (!voice->active) continue;
            mixed += (voice->current + (voice->next - voice->current) *
                      (int32_t)(voice->phase >> 8) / 256) / 3;
            voice->phase += SFX_PHASE_STEP;
            if (voice->phase >= 65536u) {
                voice->phase -= 65536u;
                voice->current = voice->next;
                voice->next = sfx_decode(voice);
                if (voice->position >= dubsiren_sample_info[voice->sample].frames &&
                    voice->current == 0 && voice->next == 0)
                    voice->active = 0u;
            }
        }
        if (mixed > 20000) mixed = 20000;
        if (mixed < -20000) mixed = -20000;
        out[2u * i] += mixed;
        out[2u * i + 1u] += mixed;
    }
}

#endif
