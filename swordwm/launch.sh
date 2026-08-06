#!/bin/bash
# =========================================================
# launch.sh — SwordWM full-stack session launcher
#
# Replaces i3. Called from ~/.xinitrc or a display manager.
# Starts: swordwm (WM) + animated wallpaper + sworddeck bar
#
# Usage:
#   startx ~/.config/swordwm/launch.sh   (TTY login)
#   Or set swordwm.desktop as your session in the DM
# =========================================================

export DISPLAY="${DISPLAY:-:0}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SWORDWM_DIR="$HOME/SwordWM"

# ── Environment ───────────────────────────────────────────
export XDG_SESSION_TYPE=x11
export XDG_CURRENT_DESKTOP=swordwm

# ── Copy default config if user has none ──────────────────
mkdir -p "$HOME/.config/swordwm"
if [ ! -f "$HOME/.config/swordwm/config" ]; then
    if [ -f "$SWORDWM_DIR/swordwm/config/swordwm.conf" ]; then
        cp "$SWORDWM_DIR/swordwm/config/swordwm.conf" \
           "$HOME/.config/swordwm/config"
        echo "[launch.sh] Installed default config to ~/.config/swordwm/config"
    fi
fi

# ── X settings ────────────────────────────────────────────
# Keyboard repeat rate
xset r rate 300 30 2>/dev/null || true
# Disable screen blanking
xset s off -dpms 2>/dev/null || true

# ── Compositor (optional — comment out if not installed) ──
if command -v picom &>/dev/null; then
    picom --daemon --backend glx --vsync 2>/dev/null &
fi

# ── Animated wallpaper ────────────────────────────────────
# C wallpaper (fast, low RAM) — uses last saved mode
if [ -x "$SWORDWM_DIR/swordwm-wallpaper" ]; then
    "$SWORDWM_DIR/swordwm-wallpaper" &
elif [ -f "$SWORDWM_DIR/cyberdeck.py" ]; then
    # Fall back to Python cyberdeck
    python3 "$SWORDWM_DIR/cyberdeck.py" &
fi

# ── Status bar (C++ sworddeck — preferred) ────────────────
SWORDDECK=""
if [ -x "$SWORDWM_DIR/cpp-sworddeck/build/sworddeck" ]; then
    SWORDDECK="$SWORDWM_DIR/cpp-sworddeck/build/sworddeck"
elif [ -x "$SWORDWM_DIR/sworddeck" ]; then
    SWORDDECK="$SWORDWM_DIR/sworddeck"
fi
[ -n "$SWORDDECK" ] && "$SWORDDECK" &

# ── SwordWM (the window manager — must be last, blocks) ───
SWORDWM_BIN="$SWORDWM_DIR/swordwm/swordwm"
if [ ! -x "$SWORDWM_BIN" ]; then
    echo "[launch.sh] Building swordwm..."
    make -C "$SWORDWM_DIR/swordwm" 2>&1
fi

exec "$SWORDWM_BIN"
