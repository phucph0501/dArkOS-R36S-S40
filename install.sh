#!/bin/bash
# S40 launcher installer for dArkOS (R36S / RK3326, Debian trixie, aarch64).
# One command: installs the S40 launcher (YouTube, weather, radio, TV, browser, file manager + archives, Java/J2ME)
# next to EmulationStation and lets you switch between the two front-ends.
#
#   sudo bash install.sh [--default s40|es] [--no-default] [--bin FILE|URL] [--debs DIR] [--yes]
#
#   --default s40|es   which front-end starts at boot after the install (default: s40)
#   --no-default       install only, keep the current front-end
#   --bin FILE|URL     the prebuilt binaries archive (s40-bin-aarch64.tgz). Default: dist/s40-bin-aarch64.tgz of this repository,
#                      else downloaded from GitHub (S40_BIN_URL).
#   --debs DIR         folder with .deb files to use instead of downloading (slow Wi-Fi / offline)
#   --yes              do not ask questions
set -u
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BIN_URL_DEFAULT="${S40_BIN_URL:-https://github.com/phucph0501/dArkOS-R36S-S40/raw/main/dist/s40-bin-aarch64.tgz}"
DEFAULT=s40; BIN=""; DEBS=""; YES=0
while [ $# -gt 0 ]; do
  case "$1" in
    --default) DEFAULT="$2"; shift 2;;
    --no-default) DEFAULT=""; shift;;
    --bin) BIN="$2"; shift 2;;
    --debs) DEBS="$2"; shift 2;;
    --yes|-y) YES=1; shift;;
    -h|--help) sed -n 2,16p "$0"; exit 0;;
    *) echo "unknown option: $1" >&2; exit 1;;
  esac
done
[ "$(id -u)" -eq 0 ] || exec sudo -E bash "$0" "$@"

say()  { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33m!! %s\033[0m\n' "$*"; }
die()  { printf '\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
fetch() { # fetch URL DEST (resumable, retries: the device Wi-Fi can be slow)
  for i in 1 2 3 4 5; do curl -fsSL --retry 3 -C - -o "$2" "$1" && return 0; sleep 3; done; return 1; }

# ---------------------------------------------------------------- checks
say "Checking the system"
[ "$(uname -m)" = aarch64 ] || die "this installer is for aarch64 (R36S / RK3326)"
grep -qi debian /etc/os-release || die "this installer is for dArkOS (Debian)"
[ -x /usr/bin/emulationstation/emulationstation.sh ] || warn "EmulationStation not found: switching back to it will not work"
avail=$(df --output=avail -k / | tail -1); [ "${avail:-0}" -gt 900000 ] || die "need at least 900 MB free on the system partition"
[ -f /opt/inttools/gamecontrollerdb.txt ] || warn "/opt/inttools/gamecontrollerdb.txt missing (gamepad mapping for YouTube/TV playback)"
[ -d "$HERE/payload" ] || die "payload/ folder missing next to install.sh"
if [ "$YES" -eq 0 ]; then
  echo "This installs the S40 launcher under /opt/s40, /opt/s40web, /opt/ytdl, /opt/j2me and adds two systemd services."
  echo "EmulationStation and your games are not touched. Uninstall: sudo bash uninstall.sh"
  read -r -p "Continue? [y/N] " a; [ "$a" = y ] || [ "$a" = Y ] || exit 0
fi

# ---------------------------------------------------------------- 1. packages
say "1/7  Runtime packages (apt)"
if [ -n "$DEBS" ] && ls "$DEBS"/*.deb >/dev/null 2>&1; then
  mkdir -p /var/cache/apt/archives; cp -n "$DEBS"/*.deb /var/cache/apt/archives/
  echo "using $(ls "$DEBS"/*.deb | wc -l) local .deb files"
fi
export DEBIAN_FRONTEND=noninteractive
PKGS="libwpewebkit-2.0-1 libwpebackend-fdo-1.0-1 libwpe-1.0-1 libx11-xcb1 libxcb-dri2-0 fonts-wqy-microhei libvterm0 \
gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-libav gstreamer1.0-alsa mpv python3 espeak-ng-data xz-utils curl ca-certificates"
if ! apt-get install -y -q --no-install-recommends $PKGS; then
  echo "(apt could not install everything from the cache: refreshing the package lists)"
  apt-get update -q && apt-get install -y -q --no-install-recommends $PKGS || die "apt failed"
fi
# Java for the J2ME runner (JRE 21; dArkOS usually has one already)
JAVA=""
for j in /usr/lib/jvm/java-21-openjdk-arm64 /usr/lib/jvm/default-java /usr/lib/jvm/java-17-openjdk-arm64 /opt/j2me-jre; do
  [ -x "$j/bin/java" ] && "$j/bin/java" -version 2>&1 | grep -q 'version "\(21\|2[2-9]\)' && { JAVA="$j"; break; }
done
if [ -z "$JAVA" ]; then
  apt-get install -y -q --no-install-recommends openjdk-21-jre-headless || warn "Java 21 not installed: J2ME games will not run"
  [ -x /usr/lib/jvm/java-21-openjdk-arm64/bin/java ] && JAVA=/usr/lib/jvm/java-21-openjdk-arm64
fi

# ---------------------------------------------------------------- 2. binaries
say "2/7  Prebuilt binaries (launcher, browser, gamepad bridge, J2ME runner)"
TMPD=$(mktemp -d); trap 'rm -rf "$TMPD"' EXIT
if [ -z "$BIN" ]; then
  if [ -f "$HERE/s40-bin-aarch64.tgz" ]; then BIN="$HERE/s40-bin-aarch64.tgz"
  elif [ -f "$HERE/dist/s40-bin-aarch64.tgz" ]; then BIN="$HERE/dist/s40-bin-aarch64.tgz"
  else BIN="$BIN_URL_DEFAULT"; fi
fi
case "$BIN" in
  http*) echo "downloading $BIN"; fetch "$BIN" "$TMPD/bin.tgz" || die "could not download the binaries"; BIN="$TMPD/bin.tgz";;
  *) [ -f "$BIN" ] || die "binaries archive not found: $BIN";;
esac
tar -xzf "$BIN" -C / --no-same-owner || die "could not unpack the binaries"
# archives made on Windows carry no execute bits
chmod 755 /opt/s40/s40 /opt/s40web/s40web /opt/ytdl/ytpad /opt/j2me/engine/sdl_interface 2>/dev/null
chown -R root:root /opt/s40web /opt/j2me 2>/dev/null
chmod -R a+rX /opt/s40web /opt/j2me
[ -x /opt/s40/s40 ] || die "launcher binary missing after unpacking"

# ---------------------------------------------------------------- 3. files
say "3/7  Launcher files, scripts and services"
cp -a "$HERE/payload/." /
find /opt/s40 /opt/ytdl /usr/local/bin/switch-launcher.sh /usr/local/bin/s40-lowbat.sh /etc/systemd/system/s40.service \
     /etc/systemd/system/s40-lowbat.service -type f \( -name '*.sh' -o -name '*.service' \) -exec sed -i 's/\r$//' {} + 2>/dev/null
chmod 755 /opt/s40/s40 /opt/s40/s40.sh /opt/s40/bin/mpv /opt/ytdl/*.sh /usr/local/bin/switch-launcher.sh /usr/local/bin/s40-lowbat.sh /usr/local/bin/s40-tune.sh
[ -f /opt/ytdl/ytpad ] && chmod 755 /opt/ytdl/ytpad
touch /opt/s40/.device
# real GLVND EGL/GLES + wayland-egl for the WebKit web process and mpv (libmali replaces the system ones)
if [ ! -e /opt/s40web/glvnd/libEGL.so.1 ] || [ ! -e /opt/s40web/wlegl/libwayland-egl.so.1 ]; then
  G=$(mktemp -d); ( cd "$G" && apt-get download libegl1 libgles2 libglvnd0 libwayland-egl1 >/dev/null 2>&1 && for d in *.deb; do dpkg -x "$d" x; done )
  L=$G/x/usr/lib/aarch64-linux-gnu
  mkdir -p /opt/s40web/glvnd /opt/s40web/wlegl
  cp -a $L/libEGL.so.1* $L/libGLESv2.so.2* $L/libGLdispatch.so.0* /opt/s40web/glvnd/ 2>/dev/null
  cp -a $L/libwayland-egl.so.1* /opt/s40web/wlegl/ 2>/dev/null
  rm -rf "$G"
fi
[ -e /opt/s40web/glvnd/libEGL.so.1 ] || warn "GLVND libraries missing: the browser will not draw pages"

# ---------------------------------------------------------------- 4. third-party tools (official sources, not bundled)
say "4/7  yt-dlp, Node.js and 7-Zip (official downloads)"
mkdir -p /opt/ytdl/node/bin /opt/s40/bin
if [ ! -d /opt/ytdl/pkg ]; then
  fetch https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp "$TMPD/yt-dlp" || warn "yt-dlp download failed"
  [ -s "$TMPD/yt-dlp" ] && python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$TMPD/yt-dlp" /opt/ytdl/pkg
fi
if [ ! -x /opt/ytdl/node/bin/node ]; then
  NV=${NODE_VER:-v22.23.5}
  if fetch "https://nodejs.org/dist/$NV/node-$NV-linux-arm64.tar.xz" "$TMPD/node.tar.xz"; then
    mkdir -p "$TMPD/nx"; tar -xJf "$TMPD/node.tar.xz" -C "$TMPD/nx" --strip-components=1 "node-$NV-linux-arm64/bin/node" && install -m 755 "$TMPD/nx/bin/node" /opt/ytdl/node/bin/node
  else warn "Node.js download failed: YouTube will be slow or fail"; fi
fi
if [ ! -x /opt/s40/bin/7zzs ]; then
  if fetch https://www.7-zip.org/a/7z2501-linux-arm64.tar.xz "$TMPD/7z.tar.xz" ||      fetch https://github.com/ip7z/7zip/releases/download/25.01/7z2501-linux-arm64.tar.xz "$TMPD/7z.tar.xz"; then
    mkdir -p "$TMPD/7z"; tar -xJf "$TMPD/7z.tar.xz" -C "$TMPD/7z"
    s=$(ls "$TMPD/7z"/7zzs "$TMPD/7z"/7zz 2>/dev/null | head -1); [ -n "$s" ] && install -m 755 "$s" /opt/s40/bin/7zzs
  else warn "7-Zip download failed: zip/rar/7z extraction will not work"; fi
fi
[ -d /opt/ytdl/pkg ] && python3 -m compileall -q /opt/ytdl/pkg >/dev/null 2>&1
# Java for the J2ME runner is looked up at /opt/j2me-jre
[ -n "$JAVA" ] && { [ -e /opt/j2me-jre ] && [ ! -L /opt/j2me-jre ] || ln -sfn "$JAVA" /opt/j2me-jre; }

# ---------------------------------------------------------------- 5. EmulationStation integration
say "5/7  EmulationStation: Java (J2ME) system and the switch tool"
ES=/etc/emulationstation/es_systems.cfg
if [ -f "$ES" ] && ! grep -q '<name>j2me</name>' "$ES" && [ -s "$HERE/extra/j2me_system.xml" ]; then
  cp -n "$ES" "$ES.before-s40"
  python3 - "$HERE/extra/j2me_system.xml" <<'PY'
import sys
s=open('/etc/emulationstation/es_systems.cfg',encoding='utf-8').read()
blk=open(sys.argv[1],encoding='utf-8').read()
s=s.replace('</systemList>', blk+'\n</systemList>',1)
open('/etc/emulationstation/es_systems.cfg','w',encoding='utf-8').write(s)
PY
fi
mkdir -p /roms/j2me; chown ark:ark /roms/j2me 2>/dev/null
mkdir -p /opt/system/Tools
cat > "/opt/system/Tools/Switch to S40 Launcher.sh" <<'EOF'
#!/bin/bash
# Make the S40 launcher the default front-end (EmulationStation stays installed).
sudo /usr/local/bin/switch-launcher.sh s40
EOF
chmod 755 "/opt/system/Tools/Switch to S40 Launcher.sh"; chown ark:ark "/opt/system/Tools/Switch to S40 Launcher.sh" 2>/dev/null
chown -R ark:ark /opt/s40 /opt/ytdl 2>/dev/null

# ---------------------------------------------------------------- 6. services
say "6/7  Services"
systemctl daemon-reload
systemctl enable --now s40-lowbat.service >/dev/null 2>&1 && echo "low-battery voice warning enabled"
systemctl enable s40-tune.service >/dev/null 2>&1
/usr/local/bin/s40-tune.sh apply && echo "emulators tuned for the R36S (re-applied at every boot, so OS updates do not undo it)"
systemctl disable s40.service >/dev/null 2>&1   # only the switch script enables it

# ---------------------------------------------------------------- 7. front-end
say "7/7  Front-end"
case "$DEFAULT" in
  s40|es) echo "switching the default front-end to: $DEFAULT"; INVOCATION_ID=manual /usr/local/bin/switch-launcher.sh "$DEFAULT";;
  *) echo "front-end left unchanged";;
esac
say "Done"
echo "S40 launcher installed. Switch back and forth:"
echo "  - in S40:  Power menu -> 'Set EmulationStation as default'"
echo "  - in EmulationStation: Tools -> 'Switch to S40 Launcher'"
echo "  - from a terminal: sudo switch-launcher.sh s40|es"
