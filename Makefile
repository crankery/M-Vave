.DEFAULT_GOAL := help

DINGHY := dinghy
PACKAGE ?= $(DINGHY)/build/dinghy.fwsc
VENV_PY := $(CURDIR)/.venv/bin/python3
export PATH := $(CURDIR)/.venv/bin:$(PATH)

.PHONY: help setup bootstrap venv roms host build test info install clean

help:
	@printf '%s\n' \
	  'make setup    Fetch Dinghy (if needed) and install Python dependencies' \
	  'make host     Render the dry DMX drum app on this computer' \
	  'make build    Build the FM-1 package: dinghy/build/dinghy.fwsc' \
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

host: roms
	@test -f "$(DINGHY)/tests/run_host.sh" || { echo 'Run make setup first.' >&2; exit 1; }
	@cd "$(DINGHY)" && tests/run_host.sh ../dmx/host_demo.txt

build: roms
	@test -f "$(DINGHY)/build.sh" || { echo 'Run make setup first.' >&2; exit 1; }
	@cd "$(DINGHY)" && ./build.sh

test: build
	@cd "$(DINGHY)" && tests/run_tests.sh

info:
	@test -f "$(DINGHY)/tools/fm1_install.py" || { echo 'Run make setup first.' >&2; exit 1; }
	@python3 "$(DINGHY)/tools/fm1_install.py" --info

install:
	@test -f "$(PACKAGE)" || { echo 'Package missing. Run make build first, or set PACKAGE=path/to/file.fwsc.' >&2; exit 1; }
	@python3 "$(DINGHY)/tools/fm1_install.py" "$(PACKAGE)"

clean:
	@rm -rf "$(DINGHY)/build"
