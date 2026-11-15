/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MT32_REVERB_H
#define MT32_REVERB_H

/* Four unequal delay lines with an orthogonal feedback mix. This is an
 * original, compact room-like effect, not the MT-32's reverb algorithm. */
#define REV_N0 1277u
#define REV_N1 1601u
#define REV_N2 1999u
#define REV_N3 2477u

static int16_t rev_0[REV_N0], rev_1[REV_N1], rev_2[REV_N2], rev_3[REV_N3];
static uint16_t rev_i0, rev_i1, rev_i2, rev_i3;
static int32_t rev_lp0, rev_lp1, rev_lp2, rev_lp3;
static volatile uint8_t rev_wet_q8 = 165u;    /* 65% of the wet signal */
static volatile uint8_t rev_feedback = 208u;  /* tail setting 6 of 10 */

static int16_t rev_clip(int32_t sample)
{
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    return (int16_t)sample;
}

static void mt32_reverb_process(int32_t *out, uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; i++) {
        int32_t a = rev_0[rev_i0], b = rev_1[rev_i1];
        int32_t c = rev_2[rev_i2], d = rev_3[rev_i3];
        int32_t input = (out[2u * i] + out[2u * i + 1u]) / 8;
        int32_t f0, f1, f2, f3;

        /* Damping keeps high frequencies from ringing through the tail. */
        rev_lp0 += (a - rev_lp0) / 4;
        rev_lp1 += (b - rev_lp1) / 4;
        rev_lp2 += (c - rev_lp2) / 4;
        rev_lp3 += (d - rev_lp3) / 4;
        f0 = (rev_lp0 + rev_lp1 + rev_lp2 + rev_lp3) / 2;
        f1 = (rev_lp0 - rev_lp1 + rev_lp2 - rev_lp3) / 2;
        f2 = (rev_lp0 + rev_lp1 - rev_lp2 - rev_lp3) / 2;
        f3 = (rev_lp0 - rev_lp1 - rev_lp2 + rev_lp3) / 2;
        rev_0[rev_i0] = rev_clip(input + f0 * rev_feedback / 256);
        rev_1[rev_i1] = rev_clip(input + f1 * rev_feedback / 256);
        rev_2[rev_i2] = rev_clip(input + f2 * rev_feedback / 256);
        rev_3[rev_i3] = rev_clip(input + f3 * rev_feedback / 256);

        if (++rev_i0 == REV_N0) rev_i0 = 0;
        if (++rev_i1 == REV_N1) rev_i1 = 0;
        if (++rev_i2 == REV_N2) rev_i2 = 0;
        if (++rev_i3 == REV_N3) rev_i3 = 0;

        out[2u * i] += ((a + c) / 2) * rev_wet_q8 / 256;
        out[2u * i + 1u] += ((b + d) / 2) * rev_wet_q8 / 256;
    }
}

#endif
