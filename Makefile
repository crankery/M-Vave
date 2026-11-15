.DEFAULT_GOAL := help

DINGHY := dinghy
# Change this line to choose the firmware app built by make.
TARGET := dmx
OUTPUT := build/$(TARGET).fwsc
PACKAGE ?= $(OUTPUT)
VENV_PY := $(CURDIR)/.venv/bin/python3
export PATH := $(CURDIR)/.venv/bin:$(PATH)

ifeq ($(TARGET),dmx)
export DINGHY_PRODUCT_PREFIX := FM-1_94
export DINGHY_SCREEN_NAME := DMX LAB
export DINGHY_SCREEN_COLOUR := 0xFCC8
ROM_PREREQ := roms
HOST_SCRIPT := ../dmx/host_demo.txt
else ifeq ($(TARGET),experiment)
export DINGHY_PRODUCT_PREFIX := FM-1_99
export DINGHY_SCREEN_NAME := DINGHY
export DINGHY_SCREEN_COLOUR := 0xB47F
else ifeq ($(TARGET),mt32)
# The Sound List's class names; use `make mt32-list` to see preset counts.
MT32_CLASSES := piano organ keyboard sbrass synbass synth1 synth2 strings guitar bass wind1 wind2 brass mallet special percussion effects
# Bank estimate guard; the device build also checks the actual app image fit.
MT32_ROM_BUDGET := 450560
export DINGHY_PRODUCT_PREFIX := FM-1_98
export DINGHY_SCREEN_NAME := MT32 LA
export DINGHY_SCREEN_COLOUR := 0xB47F
ROM_PREREQ := mt32_roms
HOST_SCRIPT := ../mt32/host_demo.txt
else
$(error Unknown TARGET '$(TARGET)'; choose dmx, experiment, or mt32)
endif
export DINGHY_APP_SOURCE := firmware/src/$(TARGET).c

.PHONY: help setup bootstrap venv roms mt32_roms mt32-list host build test info install clean

help:
	@printf '%s\n' \
	  'make setup    Fetch Dinghy (if needed) and install Python dependencies' \
	  'TARGET in this Makefile selects dmx (default), experiment, or mt32' \
	  'make mt32-list  Show MT-32 sound classes and PCM partial counts' \
	  'make host     Render the selected app on this computer' \
	  'make build    Build the selected FM-1 package: build/$(TARGET).fwsc' \
	  'make test     Build, then run Dinghy package and host tests' \
	  'make info     Show the connected FM-1 firmware identity' \
	  'make install  Install PACKAGE on the connected FM-1 (asks before writing)' \
	  'make clean    Remove generated Dinghy build files'

setup: bootstrap venv
	@echo 'Setup complete. Try make host.'

bootstrap:
	@if [ ! -f "$(DINGHY)/build.sh" ]; then ./bootstrap.sh; fi

venv:
	@if [ ! -x "$(VENV_PY)" ]; then python3 -m venv .venv; fi
	@"$(VENV_PY)" -m pip install Pillow mido python-rtmidi

roms:
	@python3 tools/dmx_embed.py
	@python3 tools/dmx_decode.py --c-header dinghy/firmware/src/dmx_decode_table.h

mt32_roms:
	@python3 tools/mt32_embed.py --classes "$(MT32_CLASSES)" --max-bytes "$(MT32_ROM_BUDGET)"

mt32-list:
	@python3 tools/mt32_embed.py --list-classes

host: $(ROM_PREREQ)
	@test -f "$(DINGHY)/tests/run_host.sh" || { echo 'Run make setup first.' >&2; exit 1; }
	@cd "$(DINGHY)" && tests/run_host.sh $(HOST_SCRIPT)

build: $(ROM_PREREQ)
	@test -f "$(DINGHY)/build.sh" || { echo 'Run make setup first.' >&2; exit 1; }
	@cd "$(DINGHY)" && ./build.sh
	@mkdir -p build
	@cp "$(DINGHY)/build/dinghy.fwsc" "$(OUTPUT)"

test: build
	@cd "$(DINGHY)" && sh tests/run_tests.sh

info:
	@test -f "$(DINGHY)/tools/fm1_install.py" || { echo 'Run make setup first.' >&2; exit 1; }
	@python3 "$(DINGHY)/tools/fm1_install.py" --info

install:
	@test -f "$(PACKAGE)" || { echo 'Package missing. Run make build first, or set PACKAGE=path/to/file.fwsc.' >&2; exit 1; }
	@python3 "$(DINGHY)/tools/fm1_install.py" "$(PACKAGE)"

clean:
	@rm -rf "$(DINGHY)/build"
	@rm -f build/dmx.fwsc build/experiment.fwsc build/mt32.fwsc
