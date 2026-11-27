#!/bin/bash
# Play one video for the S40 YouTube app with mpv. The gamepad is read by ytpad, which sends commands to mpv's IPC socket
# (keyboard emulation never reaches mpv when it draws straight to the screen).
#   ytplay.sh <video-url> [audio-url]       (audio-url only when video and audio are separate streams)
# Pad: A pause/play, B back, left/right seek 10 s (held: 20, 30, 60 s per repeat, 60 s max), up/down video brightness,
#      L1/R1 volume, X mute.
V="$1"; A="$2"
SOCK=/tmp/ytplay.sock
rm -f "$SOCK"
export SDL_GAMECONTROLLERCONFIG_FILE=/opt/inttools/gamecontrollerdb.txt
/opt/ytdl/ytpad "$SOCK" >/tmp/ytpad.log 2>&1 &
PAD=$!
EXTRA=""
[ -n "$A" ] && EXTRA="--audio-file=$A"
/opt/s40/bin/mpv --no-config --fullscreen --keepaspect=yes --hwdec=auto --vd-lavc-threads=4 --framedrop=vo   --cache=yes --demuxer-max-bytes=24MiB --demuxer-max-back-bytes=4MiB --terminal=no --osd-level=1 --log-file=/tmp/ytplay.log   --input-ipc-server="$SOCK" --user-agent="Mozilla/5.0 (Linux; Android 10) AppleWebKit/537.36" $EXTRA "$V"
RC=$?
kill $PAD 2>/dev/null; rm -f "$SOCK"
exit $RC
