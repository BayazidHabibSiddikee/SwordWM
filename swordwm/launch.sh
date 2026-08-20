#!/bin/bash
# =========================================================
# launch.sh — SwordWM session launcher
#
# Called from ~/.xinitrc or a display manager session.
#
# Usage:
#   startx ~/.config/swordwm/launch.sh   (TTY login)
#   Or install swordwm.desktop to /usr/share/xsessions/
#
# Optional components (wallpaper, bar) are only started if
# their pre-built binaries exist — nothing is auto-compiled
# or auto-downloaded here.
# =========================================================

set -euo pipefail

export DISPLAY="${DISPLAY:-:0}"
SWORDWM_DIR="$HOME/SwordWM"
SWORDWM_BIN="$SWORDWM_DIR/swordwm/swordwm"

# ── Sanity check ──────────────────────────────────────────
if [ ! -x "$SWORDWM_BIN" ]; then
    echo "[launch.sh] ERROR: swordwm binary not found at $SWORDWM_BIN"
    echo "[launch.sh] Build it first:  cd $SWORDWM_DIR/swordwm && make"
    exit 1
fi

# ── Environment ───────────────────────────────────────────
export XDG_SESSION_TYPE=x11
export XDG_CURRENT_DESKTOP=swordwm

# Silence Qt's verbose "OpenType support missing for script N" font warnings.
# These appear for Arabic/Hebrew scripts on Latin-only fonts and are harmless.
export QT_LOGGING_RULES="qt.text.font.db=false"

# ── Copy default config if user has none ──────────────────
mkdir -p "$HOME/.config/swordwm"
if [ ! -f "$HOME/.config/swordwm/config" ]; then
    DEFAULT_CONF="$SWORDWM_DIR/swordwm/config/swordwm.conf"
    if [ -f "$DEFAULT_CONF" ]; then
        cp "$DEFAULT_CONF" "$HOME/.config/swordwm/config"
        echo "[launch.sh] Installed default config to ~/.config/swordwm/config"
    fi
fi

# ── X settings ────────────────────────────────────────────
xset r rate 300 30       2>/dev/null || true   # keyboard repeat
xset s off               2>/dev/null || true   # no screen saver
xset -dpms               2>/dev/null || true   # no power management blanking

# ── Compositor ────────────────────────────────────────────
# Required: the deck uses WA_TranslucentBackground, which renders black
# without a compositor. Auto-start picom if installed.
if command -v picom &>/dev/null; then
    picom --daemon --backend glx --vsync 2>/dev/null &
fi

# ── Animated wallpaper ────────────────────────────────────
# DISABLED: the cyberdeck already paints the full desktop background, and a
# separate full-screen wallpaper window stacks on top of the deck and hides
# its main/right panels. Only the deck should own the desktop layer.
# To re-enable the wallpaper, uncomment the lines below — but then the deck
# will be covered unless you stack the deck above the wallpaper.
#
# WALLPAPER_BIN="$SWORDWM_DIR/swordwm-wallpaper"
# if [ -x "$WALLPAPER_BIN" ]; then
#     "$WALLPAPER_BIN" &
# fi

# ── Optional: status bar ──────────────────────────────────
# Prefers the C++ sworddeck binary; falls back to shell script.
# Only started if the binary exists — never auto-compiled here.
#
SWORDDECK_BIN=""
if [ -x "$SWORDWM_DIR/cpp-sworddeck/build/sworddeck" ]; then
    SWORDDECK_BIN="$SWORDWM_DIR/cpp-sworddeck/build/sworddeck"
fi
if [ -n "$SWORDDECK_BIN" ]; then
    "$SWORDDECK_BIN" &
fi

# ── SwordWM — must be last, exec replaces this process ────
exec "$SWORDWM_BIN"
