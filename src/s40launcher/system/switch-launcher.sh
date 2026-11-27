#!/bin/bash
# Switch the default front-end between the S40 launcher and EmulationStation (takes effect now and on every boot).
#   sudo switch-launcher.sh s40 | es
# It re-runs itself as a transient systemd unit: stopping the launcher that called it must not kill this script.
case "$1" in
  s40) KEEP=s40; DROP=emulationstation; DROPPROC='emulationstation';;
  es)  KEEP=emulationstation; DROP=s40; DROPPROC='s40';;
  *) echo "usage: $0 s40|es" >&2; exit 1;;
esac
if [ -z "$INVOCATION_ID" ]; then
  exec systemd-run --no-block --collect --quiet --unit="switch-launcher-$1-$$" /usr/local/bin/switch-launcher.sh "$1"
fi
sleep 1
systemctl disable "$DROP" >/dev/null 2>&1
systemctl enable "$KEEP" >/dev/null 2>&1
echo "$1" > /etc/launcher-default
systemctl stop "$DROP" 2>/dev/null
# EmulationStation can be running outside its service (its wrapper restarts it itself): make sure nothing of the old front-end is left,
# otherwise it keeps the screen and the new one never shows
if [ "$1" = s40 ]; then
  pkill -TERM -f '/usr/bin/emulationstation/emulationstation.sh' 2>/dev/null; pkill -TERM -x emulationstation 2>/dev/null
  sleep 1; pkill -KILL -f '/usr/bin/emulationstation/emulationstation' 2>/dev/null
else
  pkill -TERM -x s40 2>/dev/null; sleep 1; pkill -KILL -x s40 2>/dev/null
fi
sleep 1
printf "\033c" > /dev/tty1 2>/dev/null
systemctl reset-failed "$KEEP" 2>/dev/null
systemctl restart "$KEEP"
# verify: the new front-end must be running a few seconds later, else try again
for i in 1 2 3; do sleep 3; systemctl is-active --quiet "$KEEP" && exit 0; systemctl reset-failed "$KEEP" 2>/dev/null; systemctl restart "$KEEP"; done
exit 0
