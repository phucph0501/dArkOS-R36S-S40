#!/bin/bash
# Tune dArkOS for the R36S: RetroArch (64 and 32 bit), core options, PPSSPP buttons (FN opens the menu), library links.
#   s40-tune.sh apply | status | revert | reset-retroarch
# Safe to run any number of times. Every file is backed up once (first run) in ~/.config/s40/tune/backup, only the listed keys are
# touched, and the service s40-tune.service runs "apply" at every boot, so an OS update that rewrites a file does not undo the tuning.
PROFILE=/opt/s40/tune/profile.json
BK=/home/ark/.config/s40/tune/backup
[ "$(id -u)" -eq 0 ] || exec sudo "$0" "$@"
mkdir -p "$BK"
cmd="${1:-apply}"

py() { python3 - "$cmd" "$PROFILE" "$BK" <<'PY'
import json, os, re, shutil, sys
cmd, profile, bk = sys.argv[1], sys.argv[2], sys.argv[3]
P = json.load(open(profile, encoding='utf-8'))
changed = []
def bpath(f): return os.path.join(bk, f.strip('/').replace('/', '__'))
def backup(f):
    b = bpath(f)
    if not os.path.exists(b): shutil.copy2(f, b)
def set_ra(txt, kv):
    lines = txt.split('\n'); out = dict(kv)
    for i, l in enumerate(lines):
        m = re.match(r'\s*([^=\s#][^=]*?)\s*=', l)
        if m and m.group(1) in out:
            lines[i] = '%s = "%s"' % (m.group(1), out.pop(m.group(1)))
    for k, v in out.items():
        if lines and lines[-1] == '': lines.insert(len(lines) - 1, '%s = "%s"' % (k, v))
        else: lines.append('%s = "%s"' % (k, v))
    return '\n'.join(lines)
def set_ini(txt, kv):
    lines = txt.split('\n'); out = dict(kv)
    for i, l in enumerate(lines):
        m = re.match(r'\s*([^=\[#][^=]*?)\s*=', l)
        if m and m.group(1) in out:
            lines[i] = '%s = %s' % (m.group(1), out.pop(m.group(1)))
    for k, v in out.items(): lines.append('%s = %s' % (k, v))
    return '\n'.join(lines)
def sdl_guide(txt):
    # SDL game controller db: the R36S FN button is joystick button 16; give it "guide", R3 becomes a normal stick click
    out = []
    for l in txt.split('\n'):
        if 'GO-Super Gamepad' in l:
            l = re.sub(r'(,|^)guide:b\d+', '', l)
            l = re.sub(r'(,|^)rightstick:b\d+', '', l)
            l = l.replace('leftstick:b14,', 'leftstick:b14,rightstick:b15,guide:b16,', 1)
        out.append(l)
    return '\n'.join(out)
for name, e in P.items():
    if name.startswith('_'): continue
    for f in e['files']:
        if not os.path.isfile(f): continue
        if cmd == 'revert':
            if os.path.exists(bpath(f)): shutil.copy2(bpath(f), f); changed.append(f)
            continue
        old = open(f, encoding='utf-8', errors='surrogateescape', newline='').read()
        crlf = '\r\n' in old; t = old.replace('\r\n', '\n')
        if e['style'] == 'ra': new = set_ra(t, e['set'])
        elif e['style'] == 'ini': new = set_ini(t, e['set'])
        elif e['style'] == 'sdl_guide_fn': new = sdl_guide(t)
        else: continue
        if new != t:
            if cmd == 'apply':
                backup(f)
                open(f, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(new.replace('\n', '\r\n') if crlf else new)
            changed.append(f)
print(('%d file(s) %s' % (len(changed), {'apply': 'tuned', 'status': 'differ from the profile', 'revert': 'restored'}[cmd])) + ''.join('\n  ' + c for c in changed))
PY
}

fix_libs() {  # dArkOS uses its own SDL2 build and libmali; a Debian package can repoint the links
  L=/usr/lib/aarch64-linux-gnu; custom=""
  for f in $L/libSDL2-2.0.so.0.*; do [ -f "$f" ] || continue; dpkg -S "$f" >/dev/null 2>&1 || custom=$f; done
  if [ -n "$custom" ] && [ "$(readlink -f $L/libSDL2-2.0.so.0)" != "$custom" ]; then
    ln -sf "$custom" $L/libSDL2.so; ln -sf $L/libSDL2.so $L/libSDL2-2.0.so.0; ldconfig; echo "SDL2 link repaired -> $custom"
  fi
  # the real libwayland-egl (WebKit, mpv, GStreamer) must not point at libMali
  [ "$(readlink $L/libwayland-egl.so.1)" = "libMali.so" ] && ln -sf libwayland-egl.so.1.23.1 $L/libwayland-egl.so.1 && ldconfig && echo "libwayland-egl link repaired"
}

case "$cmd" in
  apply) py; fix_libs; chown -R ark:ark /home/ark/.config/s40 2>/dev/null; true;;
  status) py;;
  revert) py;;
  reset-retroarch)  # back to the files as dArkOS had them before the first tuning, then tune again
    cmd=revert; py; cmd=apply; py;;
  *) echo "usage: $0 apply|status|revert|reset-retroarch"; exit 1;;
esac
