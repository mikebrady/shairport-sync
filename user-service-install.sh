#!/bin/sh

# user-service-installer.sh - Installs a user-level systemd service for shairport-sync

SERVICE_NAME="shairport-sync.service"
SERVICE_SOURCE="./scripts/shairport-sync.user.service"
USER_SYSTEMD_DIR="$HOME/.config/systemd/user"
SERVICE_DEST="$USER_SYSTEMD_DIR/$SERVICE_NAME"
APP_NAME="shairport-sync"

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

echo "--------------------------------------------------"
echo "Systemd User Service Installer for $APP_NAME"
echo "This script will:"
echo "  - Check for system/initd conflicts"
echo "  - Verify the app is not already running"
echo "  - Install the user service"
echo "  - Enable it to run at login"
if [ "$DRY_RUN" -eq 1 ]; then
    echo ""
    echo "Dry-run mode: no changes will be made."
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
    echo "[FAIL] A system-level systemd service for '$APP_NAME' is currently enabled." >&2
    echo "  [ADVICE] Please disable it before installing a user-level service." >&2
    exit 1
else
    echo "[OK] No conflicting system-level systemd service enabled."
fi

# Check for init.d service
if [ -x "/etc/init.d/$APP_NAME" ]; then
    echo "[FAIL] An init.d script for '$APP_NAME' exists at /etc/init.d/$APP_NAME." >&2
    echo "  [ADVICE] Please remove or disable the init.d version before proceeding." >&2
    exit 1
else
    echo "[OK] No conflicting init.d service found."
fi

# Note if the user-level service is already running
if systemctl --user is-enabled "$APP_NAME" >/dev/null 2>&1; then
    echo "[NOTE] A user-level systemd service for '$APP_NAME' is currently enabled."
fi

# Check if the application is already running
if pgrep -x "$APP_NAME" >/dev/null 2>&1; then
    echo "[FAIL] $APP_NAME is already running." >&2
    echo "  [ADVICE] Please stop $APP_NAME before proceeding." >&2
    exit 1
else
    echo "[OK] $APP_NAME is not currently running."
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

# Create the service file

if [ "$DRY_RUN" -eq 0 ]; then
    echo "Creating service file at $SERVICE_DEST..."
    cat > "$SERVICE_DEST" <<EOF
[Unit]
Description=Shairport Sync - AirPlay Audio Receiver
After=sound.target

[Service]
ExecStart=/usr/local/bin/shairport-sync --log-to-syslog

[Install]
WantedBy=default.target
EOF
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
    echo "The user-level systemd service for $APP_NAME is now installed and enabled."
fi
