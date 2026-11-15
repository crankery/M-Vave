# DMX sound ROM decoder

`tools/dmx_decode.py` converts the raw 8-bit sound EPROM bytes to 16-bit PCM.
Its integer transfer curve comes from Table 3 in `Docs/am6070.pdf`. The
generated `dinghy/firmware/src/dmx/dmx_decode_table.h` is ready for an integer-only FM-1 sample player;
it does not embed any sound ROMs.

Preview a sound on a computer:

```sh
python3 tools/dmx_decode.py roms/dmx/21KICK.BIN
```

The WAV appears at `build/dmx-preview/21KICK.wav`. The default playback rate is
24 kHz, a convenient preview setting, **not** a measured rate for that DMX
voice card. Set `--sample-rate` to hear another tuning. The ROMs have no rate
metadata, and original cards varied in playback speed. To regenerate the C
table after changing the decoder:

```sh
python3 tools/dmx_decode.py --c-header dinghy/firmware/src/dmx/dmx_decode_table.h
```

The decoder uses datasheet sign polarity (bit 7 set gives positive output).
Changing the polarity only flips the waveform; the sound should be compared
with a known reference WAV before claiming an exact DMX output match. The
original voice cards also applied analog filtering and, on some cards,
envelopes. Those are outside this byte decoder.

The standard ride and crash samples each span four consecutive 4 KiB ROMs.
Both sets are now complete. `RIDE2A2.BIN` was obtained from the
[DMX/DX EPROM archive](https://www.electrongate.com/dmxfiles/eproms.html),
which lists the original ROM order. The `DX*.BIN` and `KICK64.BIN` files are
alternative 8 KiB DX sounds available from the source menu.

## FM-1 audition app

`tools/dmx_embed.py` packs fourteen sound streams (seven 4 KiB stock card ROMs,
two 16 KiB cymbal sets, five 8 KiB DX alternatives) into
`dinghy/firmware/src/dmx/dmx_roms.h` as read-only
bytes for the FM-1's flash. `make host` regenerates the header and runs Dinghy's
host simulator with `dinghy/tests/scripts/dmx_demo.txt`. The app in `dinghy/firmware/src/dmx/dmx.c` uses
`dinghy/firmware/src/dmx/dmx_player.h`: eight one-shot channels, one active sound per
channel, integer lookup decoding, 24 kHz nominal source playback into Dinghy's
44.1 kHz stereo output. The per-channel sample playback is dry; the master
output now has a simple compressor and stereo echo.

The first 24 FM-1 keys (F3 through E5) trigger the DMX groups in order: bass,
snare, hi-hat, toms 1–3, toms 4–6, cymbal, percussion 1, percussion 2. Each
group has three consecutive keys. MIDI channel 10 notes 36–59 use the same
order, following the Kenton DMX interface default. FM-1 keys send those MIDI
notes over USB; USB and TRS MIDI Note On trigger the sounds. Note Off does not
stop a one-shot. A new sound on a channel cuts off its predecessor.

Turn SELECT to choose one of the eight channels, then KNOB 1 to set its level
(0–127); all levels start at 100. The MASTER knob remains handled by Dinghy.
The mixer reserves headroom for eight channels at maximum level.

KNOB 2 chooses the stock DMX sound (turn left) or a DX alternative (turn right)
for the selected channel. Alternatives are available for bass (`KICK64.BIN`),
snare (`DXSNARE.BIN`), hi-hat (`DXHATS.BIN`), both tom channels (`DXTOM.BIN`),
and percussion 2 (`DXSHAKE.BIN`). Cymbal and percussion 1 only have stock
sources. The choice takes effect on the next trigger; it resets to DMX at
power-on. The DX percussion ROM contains claps in its first 4 KiB and shaker
in its second 4 KiB, checked against the archive's clap preview.

KNOB 3 sets the master echo mix (0–127, default 40), and KNOB 4 sets its tail
(0–127, default 70). At mix 0, the echo is inaudible; the compressor remains
active. Two crossed delay lines at about 186 and 279 ms create short stereo
repeats, with low-pass damping in the feedback. A block-peak compressor after
the echo has a 3:1 slope above its fixed threshold and smooth attack/release.
Both effects are on the final mix, before Dinghy's MASTER volume and USB audio
tap. They are deliberately simple and reset to their defaults at power-on.

All 24 triggers now have audio. The stock `STIK.BIN` holds rimshot in its first
2 KiB and tambourine in its second; `SHAKE6.BIN` holds claps first and shaker
second. These boundaries were checked against the archive's reference WAVs.
Bass, snare, ride, tambourine, and shaker variants use different trigger levels;
tom variants use provisional playback rates. Closed and accented hi-hat share
the hat ROM; open hat presently plays that same ROM without the original card's
longer envelope. The rates, levels, and hat behavior remain provisional until
compared with DMX hardware or recordings.

### Tom output filter

Page 7 of `Docs/Oberheim_DMX_Schematics.pdf` shows the Mark II tom voice-card
DAC feeding a CEM3320 four-stage low-pass filter. The card's trigger control
also reaches the filter. Both tom channels now run through four inexpensive
integer one-pole stages, with a short downward cutoff sweep on each trigger.
The coefficients scale with each tom variant's playback rate. This reproduces
the broad low-pass and moving-cutoff behavior, without modelling the chip's
resonance or the exact analog component values. The cutoff range and decay are
audition settings; compare against a recorded DMX before treating them as
calibrated. Other channels still play without this filter.

The FM-1 prototype package uses identity `FM-1_940`, distinct from Dinghy.
Check that this identity is available before distributing a build.
