# FM-1 firmware starter

This folder is prepared to start an M-VAVE FM-1 firmware using [Dinghy](https://github.com/zvenson/dinghy), a small GPL-3.0-only community template. Dinghy supplies the hardware platform, update path, USB/TRS MIDI, audio, controls, screen, storage, and a four-voice sine synth. Its application entry point is `dinghy/firmware/src/dinghy.c`.

The source is fetched by `./bootstrap.sh` into `dinghy/`. The script records the upstream commit in `dinghy/.upstream-commit`, does not flash a device, and refuses to overwrite an existing `dinghy/` directory. The repository source could not be fetched from this environment because GitHub DNS is unavailable; run the script from a networked shell.

From this directory, `make help` lists the routine commands. Run `make setup` once, `make host` for the WAV and screen previews, and `make build` for an installable FM-1 package. `make test` builds that package and runs Dinghy's host and package tests. `make info` and `make install` talk to a connected FM-1; `make install` checks for the package and then uses Dinghy's installer, which asks before writing. `make clean` removes generated files under `dinghy/build/`.

## First run on this Mac

```sh
./bootstrap.sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install Pillow
cd dinghy
tests/run_host.sh
```

The host run writes a WAV file and screen PNGs under `dinghy/build/host/`. Edit `dinghy/firmware/src/dinghy.c`, then rerun `tests/run_host.sh` to hear and see the change. Read `dinghy/docs/API.md` for the platform calls.

This copy has a small macOS host fix in `dinghy/firmware/src/gfx.c`: the host build leaves its canvas in ordinary static storage because macOS Clang rejects the firmware-only `.pool` section name. Device builds keep the original section placement. The FM-1 host test completed on this Mac and produced `dinghy.wav` and two screen PNGs.

## Build firmware with Dinghy's documented toolchain

Dinghy's device build needs Docker running on macOS, the JieLi PI32v2 Linux toolchain, and the JieLi AC79 SDK. The toolchain and SDK are external downloads; inspect their licenses and Dinghy's `LICENSING.md` before distributing a build.

```sh
cd dinghy
tools/get_toolchain.sh
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
  https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git "$HOME/fw-AC79_AIoT_SDK"
./build.sh
```

`./build.sh` should produce `dinghy/build/dinghy.fwsc`. If the toolchain or SDK already lives elsewhere, set `JIELI_TOOLCHAIN` or `AC79_SDK` as described in Dinghy's README. This Mac currently has no Docker command or JieLi toolchain, so a device build has not been verified here.

`./build.sh` compiles `firmware/src/dinghy.c` for the FM-1, builds the USB update loader, checks the image, and packs both into the installable `.fwsc`. The WAV and PNG files from `tests/run_host.sh` are previews; they cannot be installed. Run the device build only after the host test passes, and use a newly generated package only if the build exits successfully. The default target is the FM-1; no board option is needed.

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

For your own firmware, edit `dinghy/firmware/src/dinghy.c` and repeat the host test and build. Before sharing packages, follow `dinghy/docs/MAKE-IT-YOURS.md` to assign a distinct name, storage magic, and package identity; otherwise your copy identifies itself as Dinghy (`FM-1_990`). `./build.sh --release 0.1` gives a versioned package after that identity work.

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
