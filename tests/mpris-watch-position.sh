#!/bin/sh
#
# This script displays the Position value from Shairport Sync's MPRIS interface every second.
# Give the "sessions" or "system" buus as an argument.
# Usage: ./watch-position.sh [session|system]
# Defaults to "session" bus if no argument is given.

BUS="${1:-session}"

if [ "$BUS" != "session" ] && [ "$BUS" != "system" ]; then
    echo "Usage: $0 [session|system]" >&2
    exit 1
fi

while true; do
    micros=$(dbus-send --"$BUS" --print-reply \
        --dest=org.mpris.MediaPlayer2.ShairportSync \
        /org/mpris/MediaPlayer2 \
        org.freedesktop.DBus.Properties.Get \
        string:'org.mpris.MediaPlayer2.Player' \
        string:'Position' \
        | awk '/int64/ {print $3}')

    awk -v m="$micros" 'BEGIN {
        total_sec = int(m / 1000000 + 0.5)
        hr = int(total_sec / 3600)
        min = int((total_sec % 3600) / 60)
        sec = total_sec % 60
        if (hr == 0)
            printf "%d:%02d\n", min, sec
        else
            printf "%d:%02d:%02d\n", hr, min, sec
    }'

    sleep 1
done

