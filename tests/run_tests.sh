#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Every test that runs without hardware: core/ui unit tests, then the app in fm1-base's host simulator.
set -e
cd "$(dirname "$0")/.."
make test
