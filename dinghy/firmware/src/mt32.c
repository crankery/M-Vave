/* SPDX-License-Identifier: GPL-3.0-only */
/* Small ROM-backed LA-style experiment; not an MT-32 emulator. */
#define APP_NAME "MT32 HYBRID 4"
static void app_idle(void);
#define LCD_IDLE app_idle
#include "platform.c"
#include "mt32_roms.h"
#include "mt32_reverb.h"

#define MT_VOICES 4u
#define KEY0 53u                         /* FM-1 key 0 = F3 */

typedef struct {
    uint64_t position;                  /* PCM: 16.16 samples; synth: 32-bit phase */
    uint32_t step, remaining, duration, filter_remaining, filter_duration;
    int32_t gain, target, delta, remainder, error, low1, low2;
    int32_t filter_value, filter_target, filter_delta, filter_remainder, filter_error;
    uint16_t peak;
    uint8_t kind, wave, stage, filter_stage;
} partial_state_t;

typedef struct {
    partial_state_t partial[4];
    uint32_t age;
    uint8_t note, preset, active;
} voice_t;

static voice_t voices[MT_VOICES];
static uint32_t next_age;
static uint8_t program;
static uint8_t last_note = 255u;
static uint8_t reverb_level = 65u, reverb_tail = 6u;

static void envelope_stage(partial_state_t *state, const mt32_partial_t *param, uint8_t stage)
{
    uint32_t duration;
    int32_t target;
    state->stage = stage;
    if (stage == 4u) target = 0;
    else target = ((int32_t)state->peak << 8) * param->env[stage] / 100;
    duration = 32u + 3u * param->time[stage] * param->time[stage];
    state->target = target;
    state->remaining = state->duration = duration;
    state->delta = (target - state->gain) / (int32_t)duration;
    /* Carry the fractional step; forcing a minimum whole step overshoots slow ramps. */
    state->remainder = (target - state->gain) % (int32_t)duration;
    state->error = 0;
}

static void filter_stage(partial_state_t *state, const mt32_partial_t *param, uint8_t stage)
{
    int32_t cutoff = 8 + 2 * (int32_t)param->cutoff;
    uint32_t duration = 32u + 3u * param->filter_time[stage] * param->filter_time[stage];
    if (stage < 4u)
        cutoff += (int32_t)param->filter_depth * param->filter_env[stage] / 200;
    if (cutoff > 248) cutoff = 248;
    state->filter_stage = stage;
    state->filter_target = cutoff << 8;
    state->filter_remaining = state->filter_duration = duration;
    state->filter_delta = (state->filter_target - state->filter_value) / (int32_t)duration;
    state->filter_remainder = (state->filter_target - state->filter_value) % (int32_t)duration;
    state->filter_error = 0;
}

static uint32_t partial_step(const mt32_partial_t *param, uint8_t note, uint8_t fine)
{
    /* MT-32 key-follow choices in semitones per key, scaled by 256. */
    static const int16_t keyfollow_q8[17] = {
        -256, -128, -64, 0, 32, 64, 96, 128, 160, 192, 224,
        256, 320, 384, 512, 256, 257
    };
    const uint32_t *steps = param->kind == 1u ? mt32_note_step[param->wave] : mt32_synth_step;
    int32_t pitch = 60 * 256 + ((int32_t)note - 60) * keyfollow_q8[param->keyfollow];
    uint32_t index, fraction, next;
    pitch += ((int32_t)param->coarse - 36) * 256;
    pitch += ((int32_t)fine - 50) * 256 / 100;
    if (pitch < 0) pitch = 0;
    if (pitch > 127 * 256) pitch = 127 * 256;
    index = (uint32_t)pitch >> 8;
    fraction = (uint32_t)pitch & 255u;
    next = index < 127u ? index + 1u : index;
    return steps[index] + (uint32_t)(((uint64_t)(steps[next] - steps[index]) * fraction) >> 8);
}

static void note_on(uint8_t note, uint8_t velocity)
{
    uint32_t i, p, chosen = 0, oldest = 0xFFFFFFFFu;
    voice_t *voice;
    const mt32_program_t *preset = &mt32_programs[program];
    if (!velocity || note >= 128u) return;
    for (i = 0; i < MT_VOICES; i++) {
        if (!voices[i].active) { chosen = i; break; }
        if (voices[i].age < oldest) { oldest = voices[i].age; chosen = i; }
    }
    voice = &voices[chosen];
    voice->active = 0;
    voice->note = note;
    voice->preset = program;
    voice->age = ++next_age;
    for (p = 0; p < 4u; p++) {
        const mt32_partial_t *param = &preset->partial[p];
        partial_state_t *state = &voice->partial[p];
        uint32_t effective_velocity = 127u - ((127u - velocity) * param->velo / 100u);
        state->kind = param->kind;
        state->wave = param->wave;
        state->position = 0;
        state->gain = 0;
        state->low1 = state->low2 = 0;
        state->peak = (uint16_t)(2u * effective_velocity * param->level);
        if (!state->kind || !state->peak) { state->kind = 0; continue; }
        /* These otherwise identical Trombone 1 saws cancel in our mono mix. */
        state->step = partial_step(param, note,
                                   preset->number == 90u && p < 2u ? 50u : param->fine);
        envelope_stage(state, param, 0);
        if (state->kind >= 2u) {
            state->filter_value = (8 + 2 * (int32_t)param->cutoff) << 8;
            state->filter_remaining = 0;
            filter_stage(state, param, 0);
        }
    }
    voice->active = 1;
    last_note = note;
}

static void note_off(uint8_t note)
{
    uint32_t v, p;
    for (v = 0; v < MT_VOICES; v++) {
        voice_t *voice = &voices[v];
        if (!voice->active || voice->note != note) continue;
        for (p = 0; p < 4u; p++) {
            partial_state_t *state = &voice->partial[p];
            if (state->kind && state->stage != 4u) {
                envelope_stage(state, &mt32_programs[voice->preset].partial[p], 4);
                if (state->kind >= 2u)
                    filter_stage(state, &mt32_programs[voice->preset].partial[p], 4);
            }
        }
    }
}

static void midi_in(void)
{
    uint8_t status, data1, data2;
    while (midi_get(&status, &data1, &data2)) {
        switch (status & 0xF0u) {
        case 0x90u:
            if (data2) note_on(data1, data2);
            else note_off(data1);
            break;
        case 0x80u:
            note_off(data1);
            break;
        case 0xC0u:
            if (data1 < 128u && mt32_program_index[data1] != 255u)
                program = mt32_program_index[data1];
            break;
        case 0xB0u:
            if (data1 == 120u || data1 == 123u) {
                uint32_t v;
                for (v = 0; v < MT_VOICES; v++)
                    if (voices[v].active) note_off(voices[v].note);
            }
            break;
        }
    }
}

static void app_idle(void) { midi_in(); }

static void advance_envelope(partial_state_t *state, const mt32_partial_t *param, uint8_t no_sustain)
{
    if (!state->remaining) return;
    state->gain += state->delta;
    state->error += state->remainder;
    if (state->error >= (int32_t)state->duration) {
        state->gain++;
        state->error -= (int32_t)state->duration;
    } else if (state->error <= -(int32_t)state->duration) {
        state->gain--;
        state->error += (int32_t)state->duration;
    }
    if (--state->remaining) return;
    state->gain = state->target;
    if (state->stage == 4u || (state->stage == 3u && !state->gain)) {
        state->kind = 0;
    } else if (state->stage == 3u && no_sustain) {
        envelope_stage(state, param, 4);
    } else if (state->stage < 3u) {
        envelope_stage(state, param, state->stage + 1u);
    }
}

static void advance_filter(partial_state_t *state, const mt32_partial_t *param, uint8_t no_sustain)
{
    if (!state->filter_remaining) return;
    state->filter_value += state->filter_delta;
    state->filter_error += state->filter_remainder;
    if (state->filter_error >= (int32_t)state->filter_duration) {
        state->filter_value++;
        state->filter_error -= (int32_t)state->filter_duration;
    } else if (state->filter_error <= -(int32_t)state->filter_duration) {
        state->filter_value--;
        state->filter_error += (int32_t)state->filter_duration;
    }
    if (--state->filter_remaining) return;
    state->filter_value = state->filter_target;
    if (state->filter_stage < 3u)
        filter_stage(state, param, state->filter_stage + 1u);
    else if (state->filter_stage == 3u && no_sustain)
        filter_stage(state, param, 4);
}

static int32_t partial_sample(partial_state_t *state, const mt32_partial_t *param)
{
    int32_t sample;
    if (state->kind == 1u) {
        const mt32_wave_t *wave = &mt32_waves[state->wave];
        uint64_t end = (uint64_t)wave->length << 16;
        uint32_t index, next;
        if (state->position >= end) {
            if (!wave->loop) { state->kind = 0; return 0; }
            while (state->position >= end) state->position -= end;
        }
        index = (uint32_t)(state->position >> 16);
        next = index + 1u;
        if (next == wave->length) next = wave->loop ? 0u : index;
        sample = mt32_pcm_data[wave->offset + index];
        sample += ((mt32_pcm_data[wave->offset + next] - sample) *
                   (int32_t)(state->position & 65535u)) >> 16;
        state->position += state->step;
    } else {
        uint32_t phase = (uint32_t)state->position;
        int32_t driven, cutoff = state->filter_value >> 8;
        if (state->kind == 2u) {
            uint32_t duty = 32u + 2u * param->pulse;
            sample = (phase >> 24) < duty ? (int32_t)(256u - duty) / 2 : -(int32_t)duty / 2;
        } else {
            sample = (int32_t)(phase >> 24) - 128;
        }
        state->position = (uint32_t)(phase + state->step);
        driven = (sample * 256) - ((state->low1 - state->low2) * param->resonance >> 5);
        state->low1 += ((driven - state->low1) * cutoff) >> 8;
        state->low2 += ((state->low1 - state->low2) * cutoff) >> 8;
        sample = state->low2 >> 8;
        if (sample > 127) sample = 127;
        if (sample < -128) sample = -128;
    }
    return sample * (state->gain >> 8) >> 10;
}

static void app_audio(int32_t *out, uint32_t frames)
{
    uint32_t v, i, p;
    for (v = 0; v < MT_VOICES; v++) {
        voice_t *voice = &voices[v];
        const mt32_program_t *preset = &mt32_programs[voice->preset];
        uint8_t any = 0;
        if (!voice->active) continue;
        for (i = 0; i < frames; i++) {
            int32_t part_sample[4] = {0, 0, 0, 0}, mixed = 0;
            for (p = 0; p < 4u; p++) {
                partial_state_t *state = &voice->partial[p];
                if (!state->kind) continue;
                advance_envelope(state, &preset->partial[p], preset->no_sustain);
                if (state->kind >= 2u)
                    advance_filter(state, &preset->partial[p], preset->no_sustain);
                if (state->kind) part_sample[p] = partial_sample(state, &preset->partial[p]);
                any |= state->kind;
            }
            for (p = 0; p < 2u; p++) {
                int32_t master = part_sample[2u * p], slave = part_sample[2u * p + 1u];
                uint8_t mix = preset->mix[p];
                if (mix == 1u || mix == 2u) {
                    int32_t ring = master * slave / 2048;
                    mixed += ring + (mix == 1u ? master : 0);
                } else if (mix != 2u) {
                    mixed += master + slave;
                }
            }
            out[2u * i] += mixed;
            out[2u * i + 1u] += mixed;
        }
        if (!any) voice->active = 0;
    }
    mt32_reverb_process(out, frames);
}

static void draw_program(void)
{
    char label[] = "PRESET 000", detail[] = "PCM 0  LA 0";
    uint32_t p, pcm = 0, la = 0, number = mt32_programs[program].number + 1u;
    label[7] = (char)('0' + number / 100u);
    label[8] = (char)('0' + number / 10u % 10u);
    label[9] = (char)('0' + number % 10u);
    for (p = 0; p < 4u; p++) {
        uint8_t kind = mt32_programs[program].partial[p].kind;
        pcm += kind == 1u;
        la += kind >= 2u;
    }
    detail[4] = (char)('0' + pcm);
    detail[10] = (char)('0' + la);
    draw_text_box(0, 83, 240, &FONT_L, label, RGB(180, 140, 255), 1);
    draw_text_box(0, 122, 240, &FONT_S, mt32_programs[program].name, C_WHITE, 1);
    draw_text_box(0, 147, 240, &FONT_S, detail, RGB(130, 130, 130), 1);
}

static void draw_note(void)
{
    char label[] = "LAST NOTE ---";
    if (last_note != 255u) {
        label[10] = (char)('0' + last_note / 100u);
        label[11] = (char)('0' + last_note / 10u % 10u);
        label[12] = (char)('0' + last_note % 10u);
    }
    draw_text_box(0, 174, 240, &FONT_S, label, C_WHITE, 1);
}

static void draw_reverb(void)
{
    char label[] = "REV 000%  TAIL 00";
    label[4] = (char)('0' + reverb_level / 100u);
    label[5] = (char)('0' + reverb_level / 10u % 10u);
    label[6] = (char)('0' + reverb_level % 10u);
    label[15] = (char)('0' + reverb_tail / 10u);
    label[16] = (char)('0' + reverb_tail % 10u);
    draw_text_box(0, 188, 240, &FONT_S, label, RGB(130, 130, 130), 1);
}

static void app_main(void)
{
    uint32_t previous_keys = 0, shown_program = ~0u, shown_note = ~0u, shown_reverb = ~0u;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 24, 240, &FONT_L, APP_NAME, RGB(180, 140, 255), 1);
    draw_text_box(0, 61, 240, &FONT_S, "ROM PCM + LA-STYLE", C_WHITE, 1);
    draw_text_box(0, 206, 240, &FONT_S, "SELECT / K1 REV / K2 TAIL", RGB(130, 130, 130), 1);
    for (;;) {
        uint32_t keys, down, up, key;
        int32_t turn;
        platform_poll();
        midi_in();
        turn = fm1_enc_take(ENC_SELECT);
        if (turn) {
            int32_t choice = (int32_t)program + turn;
            while (choice < 0) choice += (int32_t)MT32_PROGRAM_COUNT;
            program = (uint8_t)((uint32_t)choice % MT32_PROGRAM_COUNT);
        }
        turn = fm1_enc_take(ENC_KNOB1);
        if (turn) {
            int32_t level = (int32_t)reverb_level + 5 * turn;
            if (level < 0) level = 0;
            if (level > 100) level = 100;
            reverb_level = (uint8_t)level;
            rev_wet_q8 = (uint8_t)(level * 255 / 100);
        }
        turn = fm1_enc_take(ENC_KNOB2);
        if (turn) {
            int32_t tail = (int32_t)reverb_tail + turn;
            if (tail < 0) tail = 0;
            if (tail > 10) tail = 10;
            reverb_tail = (uint8_t)tail;
            rev_feedback = (uint8_t)(160 + 8 * tail);
        }
        keys = fm1_in.notes;
        down = keys & ~previous_keys;
        up = previous_keys & ~keys;
        previous_keys = keys;
        for (key = 0; key < 27u; key++) {
            uint8_t note = (uint8_t)(KEY0 + key);
            if (down & (1u << key)) {
                note_on(note, 100u);
                midi_send(0x90u, note, 100u);
            }
            if (up & (1u << key)) {
                note_off(note);
                midi_send(0x80u, note, 0u);
            }
        }
        if (shown_program != program) { draw_program(); shown_program = program; }
        if (shown_note != last_note) { draw_note(); shown_note = last_note; }
        if (shown_reverb != ((uint32_t)reverb_level << 8 | reverb_tail)) {
            draw_reverb(); shown_reverb = (uint32_t)reverb_level << 8 | reverb_tail;
        }
    }
}
