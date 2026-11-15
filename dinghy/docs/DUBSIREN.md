# Dub siren and lick shots

This is a small, original sound-system instrument for the FM-1. A pulse
oscillator runs through a pitch-sweep LFO and a damped 371 ms echo. The wail
charges quickly and falls more slowly; its pulse width moves with the sweep
to add the even harmonics visible in the reference spectrogram. K2
chooses WAIL (triangle), TWO TONE (square), RISE, DIVE, LASER ZAP, RATATAT,
or AIR HORN. The last three are synthetic one-shot lick shots: a falling laser,
a rapid gated burst, and a simple two-oscillator horn. The full recorded
airhorn stays on G4 as a separate sample. The synthetic shots are
intentionally cheesy.

The classic three-knob layout is on K1 (pitch offset, -12 to +12 semitones),
K2 (seven sound modes), and K3 (three stepped LFO speeds plus manual mode).
PLAY is the trigger; LFO and FX are the two manual modulation buttons. LFO
charges the pitch sweep slowly, FX charges it quickly, and releasing them
lets it fall. They work over the automatic sweep too. In manual mode, only
the buttons move the sweep.

The FM-1's extra controls set sweep depth (SELECT, 0 to 24 semitones) and echo
feedback (K4). The echo's time and wet level are fixed. Sweep speed, depth,
and manual modulation have no effect on the three one-shot modes. Pitch offset
and echo feedback still work. MASTER controls final output level.

Hold a key in the bottom octave or PLAY for a continuous siren; releasing it
lets the voice fade while the echo continues. Those keys set the starting
pitch (F3 to E4); PLAY fires C4. In the three lick-shot modes, a key press
or PLAY press fires one shot. OCT− and OCT+ transpose those siren keys and
PLAY by up to two octaves in either direction. The octave is shown on screen
and resets to zero at power-on. A held note keeps its pitch until retriggered;
incoming MIDI notes and the sample keys are not transposed.
MIDI Note On and Off on any channel work the same way. K2 changes mode.

The bottom octave, F3–E4 (keys 0–11), plays the siren. The keys above it are
sample slots. F4 plays the gun-cock effect, F#4 plays the gunshot, and G4 plays
the airhorn; G#4–G5 are available for future samples. Samples can overlap
the siren and each other. MIDI notes 53–64 trigger the siren and 65–79 map
to the sample slots in the same order.

SEL toggles sample playback between FULL (the default, each press plays the
whole sample) and GATE (each press starts at the beginning and key release
stops that sample). ENV also accepts a toggle press. The SEL LED lights in
GATE mode, and the screen shows the current setting. MIDI Note Off, including
Note On with velocity zero, gates MIDI-triggered samples in the same way.
The siren keys and synthetic shots
are unaffected. The shared echo can still ring briefly after a gated sample
is cut off.

The source MP3s in `refs/` are converted to mono 16 kHz IMA ADPCM, an
integer-only 4-bit-per-sample format. The generator removes silence using
10 ms RMS windows, retaining 20 ms before the attack and 120 ms after the
last audible window. The three samples use 43,600 bytes of flash together.
During conversion, the loudest 100 ms of each effect is brought toward
`target_window_rms_dbfs` in the manifest. Peaks are softly limited and gain
is capped at 3×, so a short click keeps its attack instead of being crushed
to the same RMS as a sustained horn. The target is set for roughly the siren's
listening level after the player's fixed one-third sample mix; adjust it and
regenerate the header if the effects need a different balance.
`dubsiren_samples.h` is checked in so normal builds do not need the MP3s or
SoX. To change or add samples, edit
`dinghy/firmware/src/dubsiren/samples.json`, put the source files in `refs/`,
and run `make dubsiren-samples` before rebuilding.

From the repository root:

```sh
make host TARGET=dubsiren
make build TARGET=dubsiren
```

The host demo runs through octave changes, the wail, three synthetic shots,
and the three samples. The package uses
local prototype identity `FM-1_960` and shows `DUB SIREN` on the update screen.
Hardware audio has not yet been checked.

The control layout follows the [Klementz NJD Box manual](https://www.klementz.fr/shop/en/index.php?controller=attachment&id_attachment=16):
pitch, four sound modes, three LFO speeds and manual, trigger, and two
modulation buttons. The lick-shot ideas come from the
[Benidub Lickshot](https://benidub.com/products/lickshot-dub-siren/). This app
does not model either device's circuit.
