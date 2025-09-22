#!/bin/sh

# exist if any command returns a non-zero result
set -e

if [ -z ${ENABLE_AVAHI+x} ] || [ $ENABLE_AVAHI -eq 0 ]; then
  rm -rf /run/dbus/dbus.pid
  rm -rf /run/avahi-daemon/pid

  dbus-uuidgen --ensure
  dbus-daemon --system
fi

(/usr/local/bin/nqptp > /dev/null 2>&1) &

[ -z ${ENABLE_AVAHI+x} ] || [ $ENABLE_AVAHI -eq 0 ] || avahi-daemon --daemonize --no-chroot

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
