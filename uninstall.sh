#!/bin/bash
# Remove the S40 launcher and go back to EmulationStation. Your games and EmulationStation are untouched.
[ "$(id -u)" -eq 0 ] || exec sudo -E bash "$0" "$@"
echo "Restoring the emulator settings that were tuned..."
[ -x /usr/local/bin/s40-tune.sh ] && /usr/local/bin/s40-tune.sh revert
echo "Switching back to EmulationStation..."
[ -x /usr/local/bin/switch-launcher.sh ] && INVOCATION_ID=manual /usr/local/bin/switch-launcher.sh es
systemctl disable --now s40-lowbat.service s40-tune.service s40.service >/dev/null 2>&1
rm -f /etc/systemd/system/s40.service /etc/systemd/system/s40-lowbat.service /etc/systemd/system/s40-tune.service /usr/local/bin/s40-tune.sh /usr/local/bin/s40-lowbat.sh /usr/local/bin/switch-launcher.sh \
      "/opt/system/Tools/Switch to S40 Launcher.sh" /etc/launcher-default
rm -rf /opt/s40 /opt/s40web /opt/ytdl /opt/j2me
[ -L /opt/j2me-jre ] && rm -f /opt/j2me-jre
[ -f /etc/emulationstation/es_systems.cfg.before-s40 ] && cp /etc/emulationstation/es_systems.cfg.before-s40 /etc/emulationstation/es_systems.cfg
systemctl daemon-reload
echo "Done. (apt packages installed for the launcher - mpv, WPE WebKit, GStreamer... - were left in place.)"
