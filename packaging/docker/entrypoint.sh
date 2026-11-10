#!/bin/sh
set -e

# If running vector-gui without an existing X11/Wayland display and DISPLAY is unset:
if [ "$1" = "vector-gui" ] && [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
    echo "[VECTOR Container] No display detected. Defaulting to QT_QPA_PLATFORM=offscreen"
    export QT_QPA_PLATFORM=offscreen
fi

exec "$@"
