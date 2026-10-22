#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# The FFmpeg port/bink links: FFmpeg 7.1.1 (LGPL 2.1 or later; no --enable-gpl, no --enable-nonfree), configured as the
# Bink demuxer and its video and audio decoders only, as static libraries, for Linux i686 and Windows i686
# (llvm-mingw). This is the recipe for tools/deps.py (the pinned-dependency build); it writes into this
# checkout's local-data/port/bink.
#   build_ffmpeg.sh [linux|windows|all]
set -euo pipefail
URL=https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz
SHA256=733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DECOMP=${DECOMP_ROOT:-$HOME/Projects/game-dev/bea-decomp}
MINGW=$DECOMP/local-data/port/llvm-mingw/bin
B=$ROOT/local-data/port/bink
mkdir -p "$B/downloads" "$B/src"
T=$B/downloads/ffmpeg-7.1.1.tar.xz
[ -f "$T" ] || curl -sSfL --max-time 300 -o "$T" "$URL"
echo "$SHA256  $T" | sha256sum -c -
[ -d "$B/src/ffmpeg-7.1.1" ] || tar -xJf "$T" -C "$B/src"
COMMON=(--enable-static --disable-shared --disable-autodetect --disable-everything --disable-programs --disable-doc
        --disable-network --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample --disable-x86asm
        --enable-decoder=bink,binkaudio_dct,binkaudio_rdft --enable-demuxer=bink --enable-protocol=file --arch=x86_32
        --enable-cross-compile)
build() {
    local target=$1; shift
    rm -rf "$B/build-$target" "$B/prefix-$target"
    mkdir -p "$B/build-$target"
    (cd "$B/build-$target" && "$B/src/ffmpeg-7.1.1/configure" --prefix="$B/prefix-$target" "${COMMON[@]}" "$@" >configure.log &&
        make -j"$(nproc)" >make.log && make install >install.log)
    echo "built $B/prefix-$target"
}
case "${1:-all}" in
linux|all) build linux --cc="clang --target=i686-pc-linux-gnu" --target-os=linux --enable-pic ;;&
windows|all) build win --cross-prefix="$MINGW/i686-w64-mingw32-" --cc="$MINGW/i686-w64-mingw32-clang" --ar="$MINGW/llvm-ar" \
    --ranlib="$MINGW/llvm-ranlib" --nm="$MINGW/llvm-nm" --target-os=mingw32 --enable-w32threads ;;
esac
