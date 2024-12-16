#!/bin/sh
while [ ! -f /var/run/avahi-daemon/pid ]; do
  echo "Warning: avahi is not running, sleeping for 1 second before trying to start shairport-sync"
  sleep 1
done
echo "Starting shairport-sync"
# pass all commandline options to shairport-sync
/usr/local/bin/shairport-sync "$@"
