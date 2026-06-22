#!/bin/sh

# exist if any command returns a non-zero result
set -e

echo "Shairport Sync Startup ($(date))"

if [ -z ${ENABLE_AVAHI+x} ] || [ $ENABLE_AVAHI -eq 1 ]; then
  rm -rf /run/dbus/dbus.pid
  rm -rf /run/avahi-daemon/pid

  dbus-uuidgen --ensure
  dbus-daemon --system

  avahi-daemon --daemonize --no-chroot
fi

# Don't launch NQPTP if it classic only

SERVICE_TYPE=""

for arg in "$@"; do
  case "$arg" in
    --service-type=classic|--service-type=airplay1)
      SERVICE_TYPE="${arg#--service-type=}"
      ;;
  esac
done

if [ -z "$SERVICE_TYPE" ]; then
  # not looking for Classic aka AirPlay 1 so start NQPTP for AirPlay 2
  echo "Starting NQPTP ($(date))"
  (/usr/local/bin/nqptp > /dev/null 2>&1) &
fi

while [ ! -f /var/run/avahi-daemon/pid ]; do
  echo "Warning: avahi is not running, sleeping for 5 seconds before trying to start shairport-sync"
  sleep 5
done

# for PipeWire
export XDG_RUNTIME_DIR=/tmp

# for PulseAudio
export PULSE_SERVER=unix:/tmp/pulseaudio.socket
export PULSE_COOKIE=/tmp/pulseaudio.cookie

echo "Finished startup tasks ($(date)), starting Shairport Sync."

exec /usr/local/bin/shairport-sync "$@"
