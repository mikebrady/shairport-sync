#!/bin/bash

DEVICE="$1"

if [ -z "$DEVICE" ]; then
  echo "Usage: $0 \"AirPlay Speaker Name\""
  exit 1
fi

osascript <<EOF
tell application "Music"
    launch

    set targetDevice to missing value
    repeat with d in every AirPlay device
        if available of d is true then
            if name of d is "$DEVICE" then
                set targetDevice to d
                exit repeat
            end if
        end if
    end repeat

    if targetDevice is missing value then
        error "AirPlay device not found: $DEVICE"
    end if

    set current AirPlay devices to {targetDevice}
end tell
EOF
