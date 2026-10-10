#!/bin/bash
# Run the radar as a systemd service: it starts at boot, restarts if it
# stops, and keeps running when your SSH session ends.
#
#   ./install-service.sh            install and start it
#   ./install-service.sh remove     stop it and take it out
#
# Logs:     journalctl -u radar -f
# Restart:  sudo systemctl restart radar      (e.g. after rebuilding)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
USER_NAME=$(stat -c %U "$DIR")
UNIT=/etc/systemd/system/radar.service

if [ "$1" = remove ]; then
    sudo systemctl disable --now radar.service 2>/dev/null || true
    sudo rm -f "$UNIT"
    sudo systemctl daemon-reload
    sudo systemctl unmask getty@tty1.service    # the login prompt can come back
    echo "radar service removed"
    exit 0
fi

[ -x "$DIR/build/radar" ] || { echo "Build it first: cmake -B build && cmake --build build -j4"; exit 1; }

sudo tee "$UNIT" >/dev/null <<UNIT
# Written by $DIR/install-service.sh
[Unit]
Description=ADS-B radar display
Wants=network-online.target
After=network-online.target
# The radar owns the screen, so no login prompt on it
Conflicts=getty@tty1.service

[Service]
User=$USER_NAME
WorkingDirectory=$DIR
ExecStart=$DIR/build/radar radar.conf
Restart=always
RestartSec=5
# Screen, touch and console access without running as root
SupplementaryGroups=video input tty
AmbientCapabilities=CAP_SYS_TTY_CONFIG

[Install]
WantedBy=multi-user.target
UNIT
sudo systemctl daemon-reload
# No login prompt on the radar's screen. Conflicts= alone isn't enough: at
# boot both start together and the prompt can end up drawing over the radar.
sudo systemctl mask getty@tty1.service
sudo systemctl enable --now radar.service
echo "radar service installed for $USER_NAME, running from $DIR"
