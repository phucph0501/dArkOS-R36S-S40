#!/bin/bash
# Start the S40 launcher on the console (KMS/DRM), the same way dArkOS starts EmulationStation.
export SDL_ASSERT="always_ignore"
export SDL_GAMECONTROLLERCONFIG_FILE="/opt/inttools/gamecontrollerdb.txt"
export TERM=linux
export XDG_RUNTIME_DIR=/run/user/$UID/
sudo chmod 666 /dev/tty1
printf "\033c" > /dev/tty1
# hide the console cursor while the UI runs
printf "\033[?25l" > /dev/tty1
cd /opt/s40
./s40 --assets /opt/s40/assets
rc=$?
printf "\033[?25h" > /dev/tty1
exit $rc
