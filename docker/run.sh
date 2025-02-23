#!/bin/sh

# exist if any command returns a non-zero result
set -e

rm -rf /var/run/dbus.pid

dbus-uuidgen --ensure
dbus-daemon --system

(/usr/local/bin/nqptp > /dev/null 2>&1) &

avahi-daemon --daemonize --no-chroot

while [ ! -f /var/run/avahi-daemon/pid ]; do
  echo "Warning: avahi is not running, sleeping for 1 second before trying to start shairport-sync"
  sleep 1
done

# for PipeWire
export XDG_RUNTIME_DIR=/tmp

# for PulseAudio
export PULSE_SERVER=unix:/tmp/pulseaudio.socket
export PULSE_COOKIE=/tmp/pulseaudio.cookie

exec /usr/local/bin/shairport-sync "$@"
