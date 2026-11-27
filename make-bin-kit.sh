#!/bin/bash
# Maintainer tool: pack the prebuilt binaries into dist/s40-bin-aarch64.tgz (the release asset install.sh downloads).
#   make-bin-kit.sh <s40 launcher binary> <dir with opt/s40web, opt/j2me from a working install> [--with-j2me-profiles]
# Only our own binaries and the J2ME runner are packed; yt-dlp, Node.js, 7-Zip, Java and the Debian libraries are fetched at install time.
set -e
S40BIN="$1"; SRC="$2"; HERE=$(cd "$(dirname "$0")" && pwd)
[ -f "$S40BIN" ] && [ -d "$SRC/opt/s40web" ] || { echo "usage: $0 <s40 binary> <install root> [--with-j2me-profiles]"; exit 1; }
T=$(mktemp -d); mkdir -p $T/opt/s40 $T/opt/s40web/ext $T/opt/s40web/lib $T/opt/ytdl $T/opt/j2me
install -m 755 "$S40BIN" $T/opt/s40/s40
install -m 755 $SRC/opt/s40web/s40web $T/opt/s40web/s40web
install -m 644 $SRC/opt/s40web/ext/s40abext.so $T/opt/s40web/ext/; install -m 644 $SRC/opt/s40web/lib/libs40gbm.so $T/opt/s40web/lib/
cp -a $SRC/opt/s40web/adblock $T/opt/s40web/
install -m 755 "$SRC/opt/ytdl/ytpad" $T/opt/ytdl/ytpad
cp -a $SRC/opt/j2me/engine $SRC/opt/j2me/jlr $SRC/opt/j2me/cfg $T/opt/j2me/
# per-game settings are generated on the device (the launcher writes them the first time a game runs); never ship someone's game list
rm -rf $T/opt/j2me/jlr/config/games
[ "${3:-}" = "--with-j2me-profiles" ] && cp -a $SRC/opt/j2me/auto $T/opt/j2me/
mkdir -p "$HERE/dist"; tar -C $T -czf "$HERE/dist/s40-bin-aarch64.tgz" .
ls -la "$HERE/dist/s40-bin-aarch64.tgz"; rm -rf $T
