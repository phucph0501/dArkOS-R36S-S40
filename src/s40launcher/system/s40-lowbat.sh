#!/bin/bash
# Low-battery voice warning only (no gauge): reads the system's own battery percentage (/sys/class/power_supply/battery/capacity,
# the number dArkOS shows) and speaks at 20 % and again at 10 %, in the UI language (vi / en / zh, ~/.config/s40/lang).
# Once per discharge; it re-arms when charging or above 25 %.
B=/sys/class/power_supply/battery
SND=/opt/s40/assets/sounds
USERN=ark; UHOME=/home/ark
warned=0
alert() {
  lang=$(head -n1 $UHOME/.config/s40/lang 2>/dev/null | tr -d ' \r\n'); case "$lang" in vi|en|zh) ;; *) lang=vi;; esac
  f=$SND/lowbat_$lang.wav; [ -f "$f" ] || return
  (
    cur=$(amixer -c0 sget Playback 2>/dev/null | grep -o '\[[0-9]*%\]' | head -1 | tr -dc 0-9)
    [ "${cur:-100}" -lt 80 ] && amixer -q -c0 sset Playback 80% 2>/dev/null
    runuser -u $USERN -- env HOME=$UHOME aplay -q "$f" 2>/dev/null
    [ "${cur:-100}" -lt 80 ] && amixer -q -c0 sset Playback "${cur}%" 2>/dev/null
  ) &
}
while true; do
  cap=$(cat $B/capacity 2>/dev/null); st=$(cat $B/status 2>/dev/null)
  case "$cap" in ''|*[!0-9]*) sleep 20; continue;; esac
  if [ "$st" = "Charging" ] || [ "$st" = "Full" ] || [ "$cap" -gt 25 ]; then warned=0
  elif [ "$cap" -le 10 ] && [ "$warned" -lt 2 ]; then warned=2; alert
  elif [ "$cap" -le 20 ] && [ "$warned" -lt 1 ]; then warned=1; alert; fi
  sleep 15
done
