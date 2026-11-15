/* SPDX-License-Identifier: GPL-3.0-only */
/* A live dub siren with a few deliberately cheesy synthetic lick shots. */
#define APP_NAME "DUB SIREN"
static void app_idle(void);
#define LCD_IDLE app_idle
#include "platform.c"
#include "dubsiren_player.h"

#define ECHO_LENGTH 16384u /* 371 ms at 44.1 kHz */
#ifdef DINGHY_HOST
#define ECHO_STORAGE
#else
#define ECHO_STORAGE __attribute__((section(".pool")))
#endif
static int16_t echo_line[ECHO_LENGTH] ECHO_STORAGE;
#undef ECHO_STORAGE

static const uint32_t INC_C9[12] = {
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static const char *const MODE_NAME[7] = {
    "WAIL", "TWO TONE", "RISE", "DIVE", "LASER ZAP", "RATATAT", "AIR HORN"};
static const uint32_t SPEED_STEP[3] = {70000u, 200000u, 600000u};

static volatile uint32_t base_inc, high_inc;
static volatile uint32_t shot_remaining;
static volatile uint8_t mode, speed = 1u, feedback = 170u, gate, slow_mod, fast_mod;
static uint32_t carrier_phase, second_phase, lfo_phase, echo_pos;
static int32_t amp, tone, dc, echo_damp, manual_sweep, sweep_smoothed;
static int32_t tune, depth = 12, octave;
static uint8_t current_note = 60u, midi_note = 255u, midi_down;
static uint8_t last_sample = 255u, sample_gate;

static uint32_t note_inc(uint32_t note)
{
    if (note > 120u)
        note = 120u;
    return INC_C9[note % 12u] >> (10u - note / 12u);
}

static void set_pitch(uint32_t note)
{
    int32_t pitch = (int32_t)note + tune;
    if (pitch < 17)
        pitch = 17;
    if (pitch > 96)
        pitch = 96;
    base_inc = note_inc((uint32_t)pitch);
    high_inc = note_inc((uint32_t)pitch + (uint32_t)depth);
}

static void fire(uint32_t note)
{
    if (note > 127u)
        return;
    current_note = (uint8_t)note;
    set_pitch(note);
    lfo_phase = 0;
    sweep_smoothed = 0;
    if (mode == 4u)
        shot_remaining = 8192u;
    else if (mode == 5u)
        shot_remaining = 16384u;
    else if (mode == 6u)
        shot_remaining = 22050u;
    else
        gate = 1u;
}

static void sample_key(uint32_t key, uint8_t midi)
{
    uint8_t sample;
    if (key < 12u || key > 26u)
        return;
    sample = dubsiren_key_sample[key - 12u];
    if (sample >= DUBSIREN_SAMPLE_COUNT)
        return;
    sfx_fire(sample, (uint8_t)key, midi, sample_gate);
    last_sample = sample;
}

static void midi_in(void)
{
    uint8_t status, note, velocity;
    while (midi_get(&status, &note, &velocity))
    {
        uint8_t command = status & 0xF0u;
        if (command == 0x90u && velocity)
        {
            if (note >= 53u && note <= 64u)
            {
                midi_note = note;
                midi_down = 1u;
                fire(note);
            }
            else if (note >= 65u && note <= 79u)
            {
                sample_key(note - 53u, 1u);
            }
        }
        else if (command == 0x80u || command == 0x90u)
        {
            if (note == midi_note)
                midi_down = 0u;
            if (note >= 65u && note <= 79u)
                sfx_release((uint8_t)(note - 53u), 1u);
        }
    }
}

static void app_idle(void) { midi_in(); }

static int16_t clip16(int32_t sample)
{
    if (sample > 32767)
        sample = 32767;
    if (sample < -32768)
        sample = -32768;
    return (int16_t)sample;
}

static void app_audio(int32_t *out, uint32_t frames)
{
    uint32_t i;
    sfx_render(out, frames);
    for (i = 0; i < frames; i++)
    {
        uint8_t shape = mode;
        uint32_t step = base_inc, sweep = 0;
        int32_t raw, dry, repeat;
        if (fast_mod)
        {
            manual_sweep += 64;
            if (manual_sweep > 65280)
                manual_sweep = 65280;
        }
        else if (slow_mod)
        {
            manual_sweep += 4;
            if (manual_sweep > 65280)
                manual_sweep = 65280;
        }
        else if (manual_sweep > 0)
        {
            manual_sweep -= 6;
            if (manual_sweep < 0)
                manual_sweep = 0;
        }
        if (shape < 4u)
        {
            uint32_t phase = lfo_phase >> 24;
            if (speed < 3u)
            {
                /* NJD-style charge comes up faster than the long fall. */
                if (shape == 0u)
                    sweep = phase < 64u ? phase * 4u : (255u - phase) * 4u / 3u;
                else if (shape == 1u)
                    sweep = phase < 128u ? 0u : 255u;
                else if (shape == 2u)
                    sweep = phase;
                else
                    sweep = 255u - phase;
                lfo_phase += SPEED_STEP[speed];
            }
            if (sweep < (uint32_t)(manual_sweep >> 8))
                sweep = (uint32_t)(manual_sweep >> 8);
            sweep_smoothed += ((int32_t)(sweep << 8) - sweep_smoothed) / 512;
            sweep = (uint32_t)sweep_smoothed >> 8;
            step += ((high_inc - step) >> 8) * sweep;
        }
        else if (shot_remaining)
        {
            if (shape == 4u)
                step += (step >> 7) * (shot_remaining >> 5);
            else if (shape == 5u)
                step += (step >> 9) * (shot_remaining >> 6);
            shot_remaining--;
        }
        carrier_phase += step;
        /* Sweep the pulse width with the pitch, producing even harmonics. */
        raw = carrier_phase < (shape < 4u ? 0x60000000u + (sweep << 22) : 0x80000000u)
                  ? 10000
                  : -10000;
        if (shape == 5u)
        {
            lfo_phase += 2921746u; /* 30 Hz bursts */
            if (lfo_phase & 0x80000000u)
                raw = 0;
        }
        else if (shape == 6u)
        {
            second_phase += step + step / 4u;
            raw = raw / 2 + ((second_phase & 0x80000000u) ? 5000 : -5000);
        }
        tone += (raw - tone) / 4;
        dc += (tone - dc) / 4096;
        if ((shape < 4u && gate) || (shape >= 4u && shot_remaining))
        {
            if (amp < 26000)
            {
                amp += 64;
                if (amp > 26000)
                    amp = 26000;
            }
        }
        else if (amp > 0)
        {
            amp -= 4;
            if (amp < 0)
                amp = 0;
        }
        dry = (tone - dc) * amp / 32768 + out[2u * i];

        repeat = echo_line[echo_pos];
        echo_damp += (repeat - echo_damp) / 4;
        echo_line[echo_pos] = clip16(dry / 2 + echo_damp * feedback / 256);
        if (++echo_pos == ECHO_LENGTH)
            echo_pos = 0;
        dry += repeat * 5 / 8;
        out[2u * i] = dry;
        out[2u * i + 1u] = dry;
    }
}

static void put_number(char *out, uint32_t value)
{
    out[0] = (char)('0' + value / 100u % 10u);
    out[1] = (char)('0' + value / 10u % 10u);
    out[2] = (char)('0' + value % 10u);
}

static void draw_controls(void)
{
    char line[] = "PITCH +00  SPEED 0";
    char second[] = "DEPTH 00  ECHO 000";
    line[6] = tune < 0 ? '-' : '+';
    line[7] = (char)('0' + (tune < 0 ? -tune : tune) / 10);
    line[8] = (char)('0' + (tune < 0 ? -tune : tune) % 10);
    line[17] = speed == 3u ? 'M' : (char)('1' + speed);
    second[6] = (char)('0' + depth / 10);
    second[7] = (char)('0' + depth % 10);
    put_number(second + 15, feedback);
    draw_text_box(0, 131, 240, &FONT_S, line, C_WHITE, 1);
    draw_text_box(0, 153, 240, &FONT_S, second, C_WHITE, 1);
}

static void draw_sample(void)
{
    char label[32] = "OCT +0  ";
    const char *name = last_sample < DUBSIREN_SAMPLE_COUNT
                           ? dubsiren_sample_info[last_sample].name
                           : "F4+ SFX";
    uint32_t i = 8u;
    label[4] = octave < 0 ? '-' : '+';
    label[5] = (char)('0' + (octave < 0 ? -octave : octave));
    while (*name && i < sizeof(label) - 1u)
        label[i++] = *name++;
    label[i] = 0;
    draw_text_box(0, 61, 240, &FONT_S, label, C_WHITE, 1);
}

static void app_main(void)
{
    uint32_t previous_keys = 0, previous_play = 0, shown_mode = 255u, shown_sample = 255u;
    uint8_t shown_gate = 0u;
    uint8_t held_note[12] = {0};
    int32_t shown_octave = 99;
    set_pitch(current_note);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 20, 240, &FONT_L, APP_NAME, RGB(255, 170, 80), 1);
    draw_sample();
    draw_text_box(0, 184, 240, &FONT_S, "SFX: FULL  SEL: TOGGLE", RGB(170, 170, 170), 1);
    draw_text_box(0, 208, 240, &FONT_S, "K1 PITCH K2 MODE K3 SPD", RGB(170, 170, 170), 1);
    draw_controls();
    for (;;)
    {
        uint32_t keys, down, up, buttons, released, play, k, controls_changed = 0;
        int32_t turn;
        platform_poll();
        midi_in();
        keys = fm1_in.notes;
        down = keys & ~previous_keys;
        up = previous_keys & ~keys;
        previous_keys = keys;
        play = (fm1_in.buttons >> BTN_PLAY) & 1u;
        slow_mod = (uint8_t)((fm1_in.buttons >> BTN_LFO) & 1u);
        fast_mod = (uint8_t)((fm1_in.buttons >> BTN_FX) & 1u);
        buttons = fm1_input_edges(&released);
        if ((buttons & (1u << BTN_OCT_DOWN)) && octave > -2)
            octave--;
        if ((buttons & (1u << BTN_OCT_UP)) && octave < 2)
            octave++;
        if (buttons & ((1u << BTN_SEL) | (1u << BTN_ENV)))
            sample_gate ^= 1u;
        for (k = 0; k < 27u; k++)
            if (down & (1u << k))
            {
                if (k < 12u)
                {
                    held_note[k] = (uint8_t)(53 + (int32_t)k + 12 * octave);
                    fire(held_note[k]);
                    midi_send(0x90u, held_note[k], 100u);
                }
                else
                {
                    sample_key(k, 0u);
                    midi_send(0x90u, 53u + k, 100u);
                }
            }
        for (k = 0; k < 27u; k++)
            if (up & (1u << k))
            {
                if (k >= 12u)
                    sfx_release((uint8_t)k, 0u);
                midi_send(0x80u, k < 12u ? held_note[k] : 53u + k, 0u);
            }
        if (play && !previous_play)
            fire((uint32_t)(60 + 12 * octave));
        previous_play = play;
        gate = ((keys & 0xFFFu) || play || midi_down) && mode < 4u;
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), (keys >> k) & 1u);
        fm1_led_key(BTN_OCT_DOWN, octave < 0);
        fm1_led_key(BTN_OCT_UP, octave > 0);
        fm1_led_key(BTN_SEL, sample_gate);
        fm1_led_key(BTN_ENV, 0);

        turn = fm1_enc_take(ENC_KNOB2);
        if (turn)
        {
            int32_t next = (int32_t)mode + turn % 7;
            if (next < 0)
                next += 7;
            mode = (uint8_t)(next % 7);
            shot_remaining = 0;
        }
        turn = fm1_enc_take(ENC_KNOB1);
        if (turn)
        {
            tune += turn;
            if (tune < -12)
                tune = -12;
            if (tune > 12)
                tune = 12;
            set_pitch(current_note);
            controls_changed = 1;
        }
        turn = fm1_enc_take(ENC_KNOB3);
        if (turn)
        {
            int32_t next = (int32_t)speed + turn;
            if (next < 0)
                next = 0;
            if (next > 3)
                next = 3;
            speed = (uint8_t)next;
            controls_changed = 1;
        }
        turn = fm1_enc_take(ENC_SELECT);
        if (turn)
        {
            depth += turn;
            if (depth < 0)
                depth = 0;
            if (depth > 24)
                depth = 24;
            set_pitch(current_note);
            controls_changed = 1;
        }
        turn = fm1_enc_take(ENC_KNOB4);
        if (turn)
        {
            int32_t value = (int32_t)feedback + turn * 4;
            if (value < 0)
                value = 0;
            if (value > 230)
                value = 230;
            feedback = (uint8_t)value;
            controls_changed = 1;
        }
        if (shown_mode != mode)
        {
            draw_text_box(0, 91, 240, &FONT_L, MODE_NAME[mode], RGB(255, 200, 70), 1);
            shown_mode = mode;
        }
        if (shown_sample != last_sample || shown_octave != octave)
        {
            draw_sample();
            shown_sample = last_sample;
            shown_octave = octave;
        }
        if (shown_gate != sample_gate)
        {
            draw_text_box(0, 184, 240, &FONT_S,
                          sample_gate ? "SFX: GATE  SEL: TOGGLE" : "SFX: FULL  SEL: TOGGLE",
                          RGB(170, 170, 170), 1);
            shown_gate = sample_gate;
        }
        if (controls_changed)
            draw_controls();
    }
}
