# SPDX-License-Identifier: GPL-3.0-or-later
# STACK — one command per target.
#   make test      unit tests of core/ and ui/ (plain cc) + the host simulator's scripted tests
#   make web       the browser emulator into app/build/emu/ (Emscripten)
#   make serve     serve it on http://localhost:8017/
#   make fw        the FM-1 package app/build/stack.fwsc (needs the JieLi toolchain: CI does this, see .github/)
#   make demos     the demo projects as backup files (build/demos/*.stk), made by the firmware in the simulator
#   make record    re-record the golden hashes after an intended change (review the PNGs in build/test-out/)
#   make tables    regenerate app/core/tables.h (the audio path's constants) from tools/gen_tables.py

# Python >= 3.10 for fm1-base's tools and emsdk (a standalone CPython here; python3 elsewhere, e.g. CI)
PY ?= $(shell for p in $$HOME/.local/python312/bin/python3.12 python3.12 python3; do \
        command -v $$p >/dev/null 2>&1 && $$p -c 'import sys; sys.exit(sys.version_info < (3, 10))' 2>/dev/null \
        && { command -v $$p; break; }; done)
EMSDK ?= $(HOME)/emsdk
# Pillow's raqm (fm1-base's font generator) loads FriBiDi at run time; a local arm64 build of it, when there is one.
# Set on each python command: macOS drops DYLD_* when it starts the (protected) /bin/sh that runs a recipe.
FRIBIDI = DYLD_FALLBACK_LIBRARY_PATH=$(HOME)/.local/fribidi/lib
CC ?= cc
CFLAGS_TEST = -std=c99 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -ffp-contract=off -fsanitize=address,undefined
APP_SRC = app/core/core.c app/core/pattern.c app/core/drums.c app/core/acid.c app/core/synth.c app/core/fm.c app/core/fxsyn.c app/core/voices.c app/core/fx.c app/core/presets.c app/core/project.c app/core/demo.c app/ui/fb.c app/ui/ui.c app/ui/ui_draw.c app/ui/cabinet.c app/ui/stack.c

.PHONY: test unit host sim web serve fw record tables demos gif clean

test: unit sim

build/test_main: tests/unit/test_main.c $(wildcard tests/unit/*.h) $(APP_SRC) $(wildcard app/core/*.h app/ui/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS_TEST) -o $@ tests/unit/test_main.c $(APP_SRC) -lm

unit: build/test_main
	@mkdir -p build/test-out
	./build/test_main tests/golden build/test-out

record: build/test_main
	@mkdir -p build/test-out
	./build/test_main tests/golden build/test-out --record

host:
	$(FRIBIDI) $(PY) base/tools/build.py app --host

sim: host
	@mkdir -p build/sim-out
	./app/build/host/stack_host app/tests/io.txt build/sim-out

web:
	EMSDK="$(EMSDK)" EM_CONFIG="$(EMSDK)/.emscripten" EMSDK_PYTHON="$(PY)" \
	    PATH="$(EMSDK)/upstream/emscripten:$(dir $(PY)):$$PATH" $(FRIBIDI) $(PY) base/tools/build.py app --web

serve:
	cd app/build/emu && $(PY) -m http.server 8017

fw:
	./build.sh

demos: host
	@mkdir -p build/demos
	./app/build/host/stack_host app/tests/demos.txt build/demos
	@ls -la build/demos/*.stk

gif: host
	@rm -rf build/gif && mkdir -p build/gif/gif
	./app/build/host/stack_host app/tests/gif.txt build/gif >/dev/null
	$(PY) tools/make_gif.py build/gif/gif docs/img/stack-build.gif

tables:
	$(PY) tools/gen_tables.py app/core/tables.h

clean:
	rm -rf build app/build
