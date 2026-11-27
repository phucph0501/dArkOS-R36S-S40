#!/bin/sh
# Update yt-dlp (official release from github.com/yt-dlp/yt-dlp) in place. YouTube changes its protection often,
# so the apps depend on this being current. Prints the new version.
set -e
D=/opt/ytdl
T=$(mktemp -d)
curl -fsSL --max-time 120 -o "$T/yt-dlp" https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp
python3 - "$T" <<'PY'
import sys, zipfile
t = sys.argv[1]
zipfile.ZipFile(t + '/yt-dlp').extractall(t + '/pkg')
PY
python3 "$T/pkg" --version >/dev/null
rm -rf "$D/pkg.new" && mv "$T/pkg" "$D/pkg.new"
rm -rf "$D/pkg.old"; [ -d "$D/pkg" ] && mv "$D/pkg" "$D/pkg.old"
mv "$D/pkg.new" "$D/pkg"; rm -rf "$D/pkg.old" "$T"
python3 "$D/pkg" --version
