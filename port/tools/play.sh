#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# port/tools/play.sh: play the Linux port on your own desktop: a normal window, sound, keyboard and mouse.
#
#     bash port/tools/play.sh [GAME ARGUMENTS]        # e.g. -level 100, or nothing for the full boot
#
# Reads the game's data from BEA_GAME_DIR (default: the Steam install), read only. Careers and settings go to
# local-data/port/play/user (BEA_USER_DIR overrides), never into the install. dxvk renders on the Intel GPU
# (BEA_PLAY_GPU=NVIDIA for the RTX: on this laptop's desktop it found no Direct3D device, 2026-10-15). Fullscreen
# shows the game's 4:3 picture pillarboxed in a desktop-fullscreen window (port/platform/common/d3d9hooks.cpp). This is for people playing; test runs use port/tools/run.py, which
# keeps everything offscreen and silent.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EXE=$ROOT/local-data/port/build/linux/bea
[ -x "$EXE" ] || { echo "build the port first: python3 port/build.py --target linux" >&2; exit 1; }
GAME=${BEA_GAME_DIR:-$HOME/.local/share/Steam/steamapps/common/Battle Engine Aquila}
[ -f "$GAME/BEA.exe" ] || { echo "no game install at $GAME (set BEA_GAME_DIR)" >&2; exit 1; }
USER_DIR=${BEA_USER_DIR:-$ROOT/local-data/port/play/user}
mkdir -p "$USER_DIR"
cd "$USER_DIR"
export BEA_GAME_DIR="$GAME" BEA_USER_DIR="$USER_DIR" DXVK_WSI_DRIVER=SDL3 \
       DXVK_FILTER_DEVICE_NAME="${BEA_PLAY_GPU:-Intel}" DXVK_LOG_LEVEL=none
exec "$EXE" "$@"
