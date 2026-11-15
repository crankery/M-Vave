/* SPDX-License-Identifier: GPL-3.0-only */
/* DMX 24-trigger audition app for the FM-1. */
#define APP_NAME "DMX LAB"
static void app_idle(void);
#define LCD_IDLE app_idle
#include "platform.c"
#include "dmx_player.h"
#include "dmx_fx.h"

#define DMX_MIDI_FIRST 36u
#define DMX_MIDI_STATUS 0x99u  /* channel 10 */

static dmx_player_t player;
static dmx_fx_t fx;
static uint32_t last_pad = DMX_PAD_COUNT;
static const char *const pad_name[DMX_PAD_COUNT] = {
    "BASS 1", "BASS 2", "BASS 3", "SNARE 1", "SNARE 2", "SNARE 3",
    "HAT CLOSED", "HAT ACCENT", "HAT OPEN", "TOM 1", "TOM 2", "TOM 3",
    "TOM 4", "TOM 5", "TOM 6", "RIDE 1", "RIDE 2", "CRASH",
    "TAMB 1", "TAMB 2", "RIMSHOT", "SHAKER 1", "SHAKER 2", "CLAPS"
};
static const char *const channel_name[DMX_CHANNEL_COUNT] = {
    "BASS", "SNARE", "HI-HAT", "TOMS 1", "TOMS 2", "CYMBAL", "PERC 1", "PERC 2"
};

static void fire(uint32_t pad, uint8_t velocity)
{
    if (pad >= DMX_PAD_COUNT || !velocity) return;
    dmx_trigger(&player, pad, velocity);
    last_pad = pad;
}

static void midi_in(void)
{
    uint8_t status, note, velocity;
    while (midi_get(&status, &note, &velocity))
        if (status == DMX_MIDI_STATUS && velocity &&
            note >= DMX_MIDI_FIRST && note < DMX_MIDI_FIRST + DMX_PAD_COUNT)
            fire(note - DMX_MIDI_FIRST, velocity);
}

static void app_idle(void) { midi_in(); }

static void app_audio(int32_t *out, uint32_t n)
{
    dmx_render(&player, out, n);
    dmx_fx_render(&fx, out, n);
}

static void append_number(char *line, uint32_t *pos, uint32_t value)
{
    if (value >= 100u) line[(*pos)++] = (char)('0' + value / 100u);
    if (value >= 10u) line[(*pos)++] = (char)('0' + value / 10u % 10u);
    line[(*pos)++] = (char)('0' + value % 10u);
}

static void draw_channel(uint32_t ch)
{
    char line[32];
    uint32_t i = 0, level = player.level[ch];
    const char *name = channel_name[ch];
    while (name[i]) { line[i] = name[i]; i++; }
    line[i++] = ' ';
    append_number(line, &i, level);
    line[i++] = ' ';
    if (player.source[ch]) {
        line[i++] = 'D'; line[i++] = 'X';
    } else {
        line[i++] = 'D'; line[i++] = 'M'; line[i++] = 'X';
    }
    line[i] = 0;
    draw_text_box(0, 175, 240, &FONT_S, line, C_WHITE, 1);
}

static void draw_fx(void)
{
    char line[32];
    uint32_t i = 0;
    const char *prefix = "ECHO ", *middle = " TAIL ";
    while (*prefix) line[i++] = *prefix++;
    append_number(line, &i, fx.wet);
    while (*middle) line[i++] = *middle++;
    append_number(line, &i, fx.tail);
    line[i] = 0;
    draw_text_box(0, 151, 240, &FONT_S, line, C_WHITE, 1);
}

static void app_main(void)
{
    uint32_t previous_keys = 0, shown = DMX_PAD_COUNT, selected = 0;
    dmx_init(&player);
    dmx_fx_init(&fx);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 22, 240, &FONT_L, APP_NAME, RGB(255, 155, 70), 1);
    draw_text_box(0, 65, 240, &FONT_S, "24 KEYS F3..E5", C_WHITE, 1);
    draw_text_box(0, 88, 240, &FONT_S, "MIDI CH10 36..59", C_WHITE, 1);
    draw_text_box(0, 109, 240, &FONT_S, "BUS COMP + ECHO", RGB(140, 140, 140), 1);
    draw_text_box(0, 201, 240, &FONT_S, "SEL CH  K1 LVL  K2 ROM", RGB(140, 140, 140), 1);
    draw_text_box(0, 221, 240, &FONT_S, "K3 ECHO  K4 TAIL", RGB(140, 140, 140), 1);
    draw_channel(selected);
    draw_fx();

    for (;;) {
        uint32_t keys, down, up, k;
        int32_t turn, level;
        platform_poll();
        keys = fm1_in.notes;
        down = keys & ~previous_keys;
        up = previous_keys & ~keys;
        previous_keys = keys;
        for (k = 0; k < DMX_PAD_COUNT; k++) {
            if (down & (1u << k)) {
                fire(k, 100);
                midi_send(DMX_MIDI_STATUS, DMX_MIDI_FIRST + k, 100u);
            }
            if (up & (1u << k))
                midi_send(0x89u, DMX_MIDI_FIRST + k, 0u);
            fm1_led_key(KEY_LED(k), player.voice[k / 3u].active && player.voice[k / 3u].pad == k);
        }
        midi_in();
        turn = fm1_enc_take(ENC_SELECT);
        if (turn) {
            int32_t next = (int32_t)selected + turn % (int32_t)DMX_CHANNEL_COUNT;
            if (next < 0) next += DMX_CHANNEL_COUNT;
            selected = (uint32_t)next % DMX_CHANNEL_COUNT;
            draw_channel(selected);
        }
        turn = fm1_enc_take(ENC_KNOB1);
        if (turn) {
            level = (int32_t)player.level[selected] + turn * 4;
            if (level < 0) level = 0;
            if (level > 127) level = 127;
            player.level[selected] = (uint8_t)level;
            draw_channel(selected);
        }
        turn = fm1_enc_take(ENC_KNOB2);
        if (turn && dmx_has_alt[selected]) {
            player.source[selected] = turn > 0 ? 1u : 0u;
            draw_channel(selected);
        }
        turn = fm1_enc_take(ENC_KNOB3);
        if (turn) {
            level = (int32_t)fx.wet + turn * 4;
            if (level < 0) level = 0;
            if (level > 127) level = 127;
            fx.wet = (uint8_t)level;
            draw_fx();
        }
        turn = fm1_enc_take(ENC_KNOB4);
        if (turn) {
            level = (int32_t)fx.tail + turn * 4;
            if (level < 0) level = 0;
            if (level > 127) level = 127;
            fx.tail = (uint8_t)level;
            draw_fx();
        }
        if (last_pad != shown) {
            shown = last_pad;
            draw_text_box(0, 130, 240, &FONT_S, pad_name[shown], RGB(255, 155, 70), 1);
        }
    }
}
