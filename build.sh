#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# STACK's firmware build: fm1-base's tools/build.py on the app (app/build/stack.fwsc). Flags pass through:
#   ./build.sh                   a development build (identity FM-1_5800)
#   ./build.sh --release 0.1     a release (FM-1_58000100, app/build/stack-0.1.fwsc)
#   ./build.sh --host | --web    the host simulator / the browser emulator (see the Makefile)
# The FM-1 build needs JieLi's toolchain (Linux x86-64; Docker on macOS) and the AC79 SDK: CI's fm1-build has both.
set -e
cd "$(dirname "$0")"
exec "${PYTHON:-python3}" base/tools/build.py app "$@"
