#!/bin/sh

# user-service-installer.sh - Installs a user-level systemd service for shairport-sync

SERVICE_NAME="shairport-sync.service"
SERVICE_SOURCE="./scripts/shairport-sync.user.service"
USER_SYSTEMD_DIR="$HOME/.config/systemd/user"
SERVICE_DEST="$USER_SYSTEMD_DIR/$SERVICE_NAME"
APP_NAME="shairport-sync"
APP_DISPLAY_NAME="Shairport Sync"

DRY_RUN=0

show_help() {
    echo "Usage: $0 [OPTION]"
    echo ""
    echo "Installs a user-level systemd service to:"
    echo "  $USER_SYSTEMD_DIR"
    echo ""
    echo "Options:"
    echo "  -h, --help       Show this help message and exit"
    echo "  --dry-run        Show what the script would do without making changes"
}

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            show_help
            exit 0
            ;;
        --dry-run)
            DRY_RUN=1
            ;;
        *)
            echo "Unknown option: $1" >&2
            show_help
            exit 1
            ;;
    esac
    shift
done

# Check we are not running as root or under sudo
if [ "$(id -u)" -eq 0 ]; then
    echo "[FAIL] This script must not be run as root or with sudo." >&2
    echo "  [ADVICE] Please run it as the regular user who will own the service." >&2
    exit 1
fi

echo "--------------------------------------------------"
echo "Systemd User Service Installer for $APP_DISPLAY_NAME"
echo "This script will:"
echo "  - Check for systemd/initd conflicts"
echo "  - Verify that Shairport Sync is not already running"
echo "  - Detect PipeWire or PulseAudio"
echo "  - Install the user service"
echo "  - Enable it to run at login"
if [ "$DRY_RUN" -eq 1 ]; then
    echo ""
    echo "Note: Dry-run mode: no changes will be made."
fi
echo "--------------------------------------------------"
echo ""

# Check for systemd
if command -v systemctl >/dev/null 2>&1; then
    echo "[OK] systemd is available."
else
    echo "[FAIL] systemd is not available on this system." >&2
    exit 1
fi

# Check for system-level service
if systemctl is-enabled "$APP_NAME" >/dev/null 2>&1; then
    echo "[FAIL] A system-level systemd service (\"$APP_NAME\") for $APP_DISPLAY_NAME is currently enabled." >&2
    echo "  [ADVICE] Please disable it before installing a user-level service. For example, try:" >&2
    echo "  [ADVICE] $ sudo systemctl disable --now $APP_NAME" >&2
    exit 1
else
    echo "[OK] No conflicting system-level systemd service enabled."
fi

# Check for init.d service
if [ -x "/etc/init.d/$APP_NAME" ]; then
    echo "[FAIL] An init.d script for $APP_DISPLAY_NAME exists at /etc/init.d/$APP_NAME." >&2
    echo "  [ADVICE] Please remove or disable the init.d version before proceeding. For example, try:" >&2
    echo "  [ADVICE] $ sudo rm /etc/init.d/$APP_NAME" >&2
    exit 1
else
    echo "[OK] No conflicting init.d service found."
fi

# Note if the user-level service is already running
if systemctl --user is-enabled "$APP_NAME" >/dev/null 2>&1; then
    echo "[NOTE] A user-level systemd service -- \"$APP_NAME\" -- for $APP_DISPLAY_NAME is currently enabled."
fi

# Check if the application is already running
if pgrep -f "$APP_NAME" >/dev/null 2>&1; then
    echo "[FAIL] $APP_DISPLAY_NAME is already running." >&2
    echo "  [ADVICE] Please stop $APP_NAME before proceeding." >&2
    exit 1
else
    echo "[OK] $APP_DISPLAY_NAME is not running."
fi

# Detect PipeWire or PulseAudio (prefer PipeWire)
AUDIO_BACKEND=""
AUDIO_REQUIRES=""
AUDIO_AFTER=""
AUDIO_OUTPUT_FLAG=""

detect_pipewire() {
    # Check if pipewire is running as a user service
    if systemctl --user is-active pipewire >/dev/null 2>&1; then
        return 0
    fi
    # Also check if the process is running
    if pgrep -x pipewire >/dev/null 2>&1; then
        return 0
    fi
    return 1
}

detect_pulseaudio() {
    # Check if pulseaudio is running as a user service
    if systemctl --user is-active pulseaudio >/dev/null 2>&1; then
        return 0
    fi
    # Also check if the process is running
    if pgrep -x pulseaudio >/dev/null 2>&1; then
        return 0
    fi
    return 1
}

# Check whether shairport-sync was built with support for a given backend
# Usage: built_with_support <backend>   e.g. built_with_support pipewire
built_with_support() {
    shairport-sync -V 2>&1 | grep -qi "$1"
}

if detect_pipewire; then
    echo "[OK] PipeWire is running."
    if built_with_support pipewire; then
        echo "[OK] $APP_DISPLAY_NAME was built with PipeWire support -- it will be configured to use it."
        AUDIO_BACKEND="pipewire"
        AUDIO_REQUIRES="Requires=pipewire.service"
        AUDIO_AFTER="After=pipewire.service"
        AUDIO_OUTPUT_FLAG="-o pipewire"
    else
        echo "[NOTE] PipeWire is active, but $APP_DISPLAY_NAME was not built with PipeWire support -- falling back to the default ALSA audio output device."
    fi
elif detect_pulseaudio; then
    echo "[OK] PulseAudio is running."
    if built_with_support pulseaudio; then
        echo "[OK] $APP_DISPLAY_NAME was built with PulseAudio support -- it will be configured to use it."
        AUDIO_BACKEND="pulseaudio"
        AUDIO_REQUIRES="Requires=pulseaudio.service"
        AUDIO_AFTER="After=pulseaudio.service"
        AUDIO_OUTPUT_FLAG="-o pulseaudio"
    else
        echo "[NOTE] PulseAudio is active, but $APP_DISPLAY_NAME was not built with PulseAudio support -- falling back to the default ALSA audio output device."
    fi
else
    echo "[NOTE] Neither PipeWire nor PulseAudio detected -- $APP_DISPLAY_NAME will use the default ALSA audio output device."
fi

# Create directory
if [ ! -d "$USER_SYSTEMD_DIR" ]; then
    if [ "$DRY_RUN" -eq 0 ]; then
        echo "Creating directory $USER_SYSTEMD_DIR..."
        mkdir -p "$USER_SYSTEMD_DIR"
        if [ $? -eq 0 ]; then
            echo "[OK] Directory $USER_SYSTEMD_DIR created."
        else
            echo "[FAIL] Failed to create directory $USER_SYSTEMD_DIR" >&2
            exit 1
        fi
    fi
else
    echo "[OK] User systemd directory already exists."
fi

if [ -d "$USER_SYSTEMD_DIR" ] && [ ! -w "$USER_SYSTEMD_DIR" ]; then
    echo "[FAIL] $USER_SYSTEMD_DIR is not writable." >&2
    echo "  [ADVICE] Please ensure $USER_SYSTEMD_DIR is owned and writable by user \"$(whoami)\" before proceeding." >&2
    exit 1
fi

# If the service file already exists, check that we can replace it
if [ -f "$SERVICE_DEST" ] && [ ! -w "$SERVICE_DEST" ]; then
    echo "[FAIL] The existing $SERVICE_DEST can not be replaced due to its ownership or permissions." >&2
    echo "  [ADVICE] Please delete $SERVICE_DEST or ensure it is owned and writable by user \"$(whoami)\" before proceeding." >&2
    exit 1
fi



# Build the service file content from a single source
if [ -n "$AUDIO_REQUIRES" ]; then
    UNIT_SECTION="[Unit]
Description=Shairport Sync - AirPlay Audio Receiver
After=sound.target
$AUDIO_REQUIRES
$AUDIO_AFTER"
else
    UNIT_SECTION="[Unit]
Description=Shairport Sync - AirPlay Audio Receiver
After=sound.target"
fi

if [ -n "$AUDIO_OUTPUT_FLAG" ]; then
    EXEC_START="ExecStart=shairport-sync --log-to-syslog $AUDIO_OUTPUT_FLAG"
else
    EXEC_START="ExecStart=shairport-sync --log-to-syslog"
fi

SERVICE_CONTENT="$UNIT_SECTION

[Service]
$EXEC_START

[Install]
WantedBy=default.target"

# Show the service file in dry-run mode, or write it for real
if [ "$DRY_RUN" -eq 1 ]; then
    echo ""
    echo "[DRY-RUN] Would write the following service file to $SERVICE_DEST:"
    echo ""
    echo "$SERVICE_CONTENT" | sed 's/^/  /'
    echo ""
else
    echo "Creating service file at $SERVICE_DEST..."
    printf '%s\n' "$SERVICE_CONTENT" > "$SERVICE_DEST"
    if [ $? -eq 0 ]; then
        echo "[OK] Service file created."
    else
        echo "[FAIL] Failed to create service file." >&2
        exit 1
    fi
fi

# Reload systemd
if [ "$DRY_RUN" -eq 0 ]; then
    echo "Reloading systemd user daemon..."
    systemctl --user daemon-reexec
    systemctl --user daemon-reload
    if [ $? -eq 0 ]; then
        echo "[OK] The systemd user daemon was reloaded."
    else
        echo "[FAIL] Failed to reload systemd user daemon." >&2
        exit 1
    fi
fi

# Enable service
if [ "$DRY_RUN" -eq 0 ]; then
    echo "Enabling service $SERVICE_NAME..."
    systemctl --user enable --now "$SERVICE_NAME"
    if [ $? -eq 0 ]; then
        echo "[OK] Service enabled and started."
    else
        echo "[FAIL] Failed to enable the user service." >&2
        exit 1
    fi
fi

echo ""
if [ "$DRY_RUN" -eq 1 ]; then
    echo "Dry run completed successfully."
else
    echo "Installation complete."
    if [ -n "$AUDIO_BACKEND" ]; then
        echo "The user-level systemd service for $APP_DISPLAY_NAME is now installed and enabled, using $AUDIO_BACKEND."
    else
        echo "The user-level systemd service for $APP_DISPLAY_NAME is now installed and enabled."
    fi
fi
