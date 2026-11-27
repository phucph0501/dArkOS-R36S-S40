#!/bin/sh
# yt-dlp for the S40 YouTube / Shorts apps. The JavaScript challenge YouTube uses needs a real JS engine:
# Node 22 (JIT) solves it in ~20 s on the RK3326, QuickJS needed ~90 s, so Node is what is installed.
# Layout: /opt/ytdl/pkg (yt-dlp, extracted so Python keeps its compiled files), /opt/ytdl/node/bin/node
D=/opt/ytdl
export HOME=${HOME:-/home/ark}
exec python3 "$D/pkg" --no-js-runtimes --js-runtimes "node:$D/node/bin/node" \
  --cache-dir "$HOME/.cache/yt-dlp" --no-warnings --socket-timeout 20 "$@"
