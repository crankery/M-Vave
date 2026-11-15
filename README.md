# FM-1 firmware experiments

## License

The original root-level build scripts, `tools/` scripts, and project-written documentation and demo scripts outside `dinghy/` are offered under the [MIT License](LICENSE). The FM-1 firmware platform and app sources in `dinghy/` retain their [GPL-3.0-only license](dinghy/LICENSE); the assembled firmware is subject to that license. The sound ROMs and third-party PDFs are not covered by this MIT license. LAX-32 is a design reference; no source from it has been incorporated here.

## Firmware targets

Set `TARGET` near the top of the root `Makefile` to `dmx`, `experiment`, `mt32`, `dinghy`, or `dubsiren`. The default is `dmx`; `experiment` and `dinghy` currently contain the original four-voice sine-wave app; `mt32` plays selected MT-32 sound classes with ROM PCM and approximate LA-style synthesized partials; `dubsiren` is a live oscillator siren with echo and synthetic lick shots. Set `MT32_CLASSES` in the Makefile and run `make mt32-list` to inspect the available classes. `make host`, `make build`, and `make test` use the selected app. You can also try `make host TARGET=dubsiren` or `make build TARGET=dinghy` without editing the file. Builds produce `build/<target>.fwsc`, and `make install` uses the selected package. The app files live in `dinghy/firmware/src/<target>/<target>.c`, alongside their own headers; shared platform files remain in `dinghy/firmware/src/`. Register future targets and their package identity and screen settings in the Makefile.

For the MT-32 experiment, run `make host TARGET=mt32` and `make build TARGET=mt32`. See [dinghy/docs/MT32.md](dinghy/docs/MT32.md) for the supported controls and limits.

For the dub siren, run `make host TARGET=dubsiren` to hear the siren, three synthetic lick shots, and three embedded sound effects, then `make build TARGET=dubsiren` for a device package. The lowest octave plays the siren; F4, F#4, and G4 play the effects. Run `make dubsiren-samples` to regenerate the compact sample data from local MP3s. See [dinghy/docs/DUBSIREN.md](dinghy/docs/DUBSIREN.md) for the controls and sample mapping.

This project uses [Dinghy](https://github.com/zvenson/dinghy) as the M-VAVE FM-1 hardware platform. The app at `dinghy/firmware/src/dmx/dmx.c` uses the first 24 FM-1 keys (F3–E5) and MIDI channel 10 notes 36–59 for the DMX's eight three-trigger groups. The stock kick, snare, hi-hat, two toms, percussion, ride, and crash ROM sets now provide audio on all 24 triggers. Each channel has one retriggerable voice and adjustable attenuation; select a channel with SELECT, adjust its level with KNOB 1, and choose a stock or DX alternative with KNOB 2 where available. KNOB 3 adjusts the master echo mix, KNOB 4 its tail, and a compressor follows the echo on the final mix. Playback is guessed at 24 kHz, decoded with an integer lookup table and mixed into Dinghy's 44.1 kHz audio output. See [dinghy/docs/DMX.md](dinghy/docs/DMX.md) for the decoder, mapping, and remaining fidelity limits.

The source is fetched by `./bootstrap.sh` into `dinghy/`. The script records the upstream commit in `dinghy/.upstream-commit`, does not flash a device, and refuses to overwrite an existing `dinghy/` directory. The repository source could not be fetched from this environment because GitHub DNS is unavailable; run the script from a networked shell.

From this directory, `make help` lists the routine commands. Run `make setup` once, `make host` for a WAV and screen preview of the seven-drum demo, and `make build` for an installable FM-1 package in `build/dmx.fwsc`. For the DMX target, both commands regenerate the ROM and decoder headers in `dinghy/firmware/src/dmx/` from the local ROM files. `make test` builds that package and runs Dinghy's host and package tests. `make info` and `make install` talk to a connected FM-1; `make install` checks for the package and then uses Dinghy's installer, which asks before writing. `make clean` removes generated files under `dinghy/build/` and the target packages under `build/`.

## First run on this Mac

```sh
./bootstrap.sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install Pillow
cd dinghy
tests/run_host.sh
```

The host run writes a WAV file and screen PNGs under `dinghy/build/host/`. Edit `dinghy/firmware/src/dmx/dmx.c` or `dinghy/firmware/src/dmx/dmx_player.h`, then rerun `make host` to hear and see the change. Read `dinghy/docs/API.md` for the platform calls.

This copy has a small macOS host fix in `dinghy/firmware/src/gfx.c`: the host build leaves its canvas in ordinary static storage because macOS Clang rejects the firmware-only `.pool` section name. Device builds keep the original section placement. The FM-1 host test completed on this Mac and produced `dinghy.wav` and two screen PNGs.

## Build firmware with Dinghy's documented toolchain

Dinghy's device build needs Docker running on macOS, the JieLi PI32v2 Linux toolchain, and the JieLi AC79 SDK. The toolchain and SDK are external downloads; inspect their licenses and Dinghy's `LICENSING.md` before distributing a build.

This Mac now has the compiler at `~/.jieli/toolchain`, the SDK at `~/fw-AC79_AIoT_SDK`, and Colima as its Docker engine. The three SDK files Dinghy uses match its pinned hashes. From the project root:

```sh
colima start   # needed after a reboot
make build     # regenerates ROM headers, then writes dinghy/build/dinghy.fwsc
```

The FM-1 device package has built successfully here, and Dinghy's host and package tests have passed. If the toolchain or SDK lives elsewhere, set `JIELI_TOOLCHAIN` or `AC79_SDK` as described in Dinghy's README.

`./build.sh` compiles the selected source in `firmware/src/` for the FM-1, builds the USB update loader, checks the image, and packs both into the installable `.fwsc`. The WAV and PNG files from `tests/run_host.sh` are previews; they cannot be installed. Run the device build only after the host test passes, and use a newly generated package only if the build exits successfully. The default target is the FM-1; no board option is needed.

## Install on the FM-1

Before the first install, make a full flash backup with [M-Flasher](https://github.com/jvitkauskas/m-flasher), back up projects, voices, and samples, and keep the stock recovery firmware available. Check your unit's flash chip: Dinghy 0.5 documents support for Puya `0x856014` and newer UC25HQ80 `0xB36014`. Read Dinghy's current install and rescue instructions in its README.

Connect the FM-1 with a USB data cable. The installer runs on macOS outside Docker and asks before writing:

```sh
# From this project's root, with .venv activated:
python3 -m pip install mido python-rtmidi
cd dinghy
python3 tools/fm1_install.py --info
python3 tools/fm1_install.py build/dinghy.fwsc
```

The DMX prototype uses package identity `FM-1_940` and shows `DMX LAB` on its app and update screens. Before sharing packages, check that identity against the community's current use and follow `dinghy/docs/MAKE-IT-YOURS.md` for the remaining release details. The bundled sound ROMs also need suitable redistribution rights before a public package is released.

## Explore the open pi32v2 toolchain

The [FM-1 developer wiki](https://github.com/sbegueria/fm1-dev-wiki) maps the hardware, boot chain, firmware bases, host simulators, installers, and recovery tools. Its [Getting Started](https://github.com/sbegueria/fm1-dev-wiki/blob/master/docs/Getting-Started.md) and [Tools](https://github.com/sbegueria/fm1-dev-wiki/blob/master/docs/Tools.md) pages cover both JieLi's build tools and the newer open [pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa) and [pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm) projects.

The open toolchain provides a pi32v2 C compiler, assembler, linker, and binary tools. Its [toolchain guide](https://github.com/jvitkauskas/pi32v2-isa/blob/main/docs/TOOLCHAIN.md) lists macOS Apple silicon and Intel release archives and a small FM-1 blink example. That gives this Mac a native route for experimenting with the CPU and building the example without JieLi's compiler or Docker. It has no C library; an application supplies startup code and a linker script. A flashable package still needs the appropriate boot and configuration files and a compatible installer or bootloader.

The open toolchain's documented hardware runs include fm1-base, Felucca, and SLOOP. Dinghy is not listed there, so treat a Dinghy build with it as an integration experiment until verified. For a new application built around the open toolchain, also look at [fm1-base](https://github.com/charlesvestal/fm1-base), an MPL-2.0 platform and template with host and browser simulators. The wiki's [Choosing a Base](https://github.com/sbegueria/fm1-dev-wiki/blob/master/docs/Choosing-a-Base.md) page compares it with Dinghy and the official kit.

## Other useful sources

- [M-VAVE FM1 Open](https://github.com/m-vave/fm1_open): official firmware source and SDK, with a binary sound engine. Its build path is Windows/PowerShell oriented.
- [Dinghy README](https://github.com/zvenson/dinghy): host workflow, device build, installer, and rescue procedure.
- [Dinghy make-it-yours guide](https://github.com/zvenson/dinghy/blob/main/docs/MAKE-IT-YOURS.md): package identity and release checklist.
- [FM-1 developer wiki](https://github.com/sbegueria/fm1-dev-wiki): community reference with source tags for measured, SDK, reverse-engineered, and unverified claims.
# M-Vave
