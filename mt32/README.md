# MT-32 hybrid experiment

This FM-1 target reads the supplied MT-32 1.07 control and PCM ROMs at build time. It plays the active partials of each selected preset: ROM PCM waves where specified, and simple pulse or saw oscillators for synthesized partials. The oscillators use approximate pitch, amplitude envelopes, and a two-pole low-pass filter using values from the control ROM. Partial pairs follow the ROM's additive or ring-modulation mix setting. It is an LA-style instrument, **not** a compatible MT-32 emulator.

Set `MT32_CLASSES` under the `mt32` target in the root `Makefile`. The default now selects all 17 sound classes: 128 presets using 46 distinct PCM waves. Run `make mt32-list` to see the groups and how many of their presets have PCM partials. `sbrass` is a useful LA-only test class: its four presets use no PCM data. Class names are space separated; the generator also accepts commas.

```sh
make host TARGET=mt32    # demo WAV and screen previews under dinghy/build/host/
make build TARGET=mt32   # build/mt32.fwsc
```

To compare Piano 1 on the bottom F3 key with middle C in the host preview, run `make host TARGET=mt32 HOST_SCRIPT=../mt32/piano_low_check.txt`.

`MT32_ROM_BUDGET` limits the estimated flash for the selected bank and generated tables. The default is 450,560 bytes; the complete bank currently needs about 283,176 bytes, including 241,664 PCM bytes. Selection fails with `Too many sounds` if it exceeds the limit. The device build independently checks actual image fit; this version's app image is 340,356 bytes of the 581,564-byte allowance, with 28,368 bytes of RAM data and BSS in the 98,304-byte allowance.

Turn SELECT to browse included presets. MIDI Program Change selects the original zero-based MT-32 preset number; unavailable presets leave the current selection unchanged. Note on/off works on any MIDI channel, as does All Notes Off. The FM-1 keys play F3–G5; MASTER remains Dinghy's output volume. KNOB 1 sets reverb level from 0 to 100%; KNOB 2 sets tail length from 0 to 10. Both start at 65% and 6. The screen shows the count of active PCM and synthesized partials, plus the reverb settings. Audio is four-note polyphonic; the dry signal is centred and the reverb tail is stereo.

The PCM waves are decoded to signed 8-bit samples with linear interpolation. Synthesized partials use pulse and saw waves, two-pole filtering, and simplified amplitude and filter envelopes. Pitch key follow and velocity response use the control-ROM settings; filter envelope depth is intentionally restrained. Amplitude and filter ramps distribute fractional steps across their full duration, avoiding a jump at the end of a slow stage. Trombone 1's two otherwise identical saw partials share their centre pitch to avoid deep mono cancellation during a held note. Envelope timing, filter key follow, and pair mixing are approximations of the LA32. Pitch envelopes, full multi-part assignments, rhythm mapping, and SysEx are not implemented. Consequently these are recognisable sketches of ROM presets, not faithful reproductions.

The reverb is an original four-line, damped feedback delay network with a stereo output. It gives a room-like tail; it does not reproduce the MT-32's four reverb algorithms. The previous PCM-only prototype was tested on a real FM-1; this hybrid version has passed host rendering and package fit checks but still needs hardware audio and interrupt-timing validation. The package has local prototype identity `FM-1_980` and shows `MT32 HYBRID 4` on screen.

The ROM parser follows Munt's [ROM loader](https://github.com/munt/munt/blob/master/mt32emu/src/Synth.cpp), [partial structures](https://github.com/munt/munt/blob/master/mt32emu/src/Structures.h), and [wave generator](https://github.com/munt/munt/blob/master/mt32emu/src/LA32FloatWaveGenerator.cpp). [LAX-32](https://github.com/pjac1810/lax32) demonstrates that a separate LA-style engine can run on an FM-1; no LAX-32 source was copied into this target. The ROMs and generated firmware contain Roland waveform data; check redistribution rights before sharing a package.
