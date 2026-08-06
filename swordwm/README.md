# SwordWM

A hybrid tiling/floating X11 window manager written in C (C11).

Built from scratch with Xlib. Inspired by dwm and i3, but designed to work
alongside the SwordWM desktop stack (swordwm-wallpaper, sworddeck, swordfm).

---

## Features

- **Three layout modes** — tile (master/stack), monocle (fullscreen), float — cycle with `Mod+Space`
- **9 workspaces** — switch with `Mod+1..9`, move windows with `Mod+Shift+1..9`
- **Title bars** — window name with focused/unfocused colour theming via Xft
- **Runtime config** — `~/.config/swordwm/config` — no recompile to change anything
- **Live reload** — `Mod+Shift+R` or `swordwm --reload` applies changes instantly
- **Autostart** — run commands at WM start from config file
- **EWMH compliant** — works with polybar, tint2, sworddeck, wmctrl, xdotool
- **ICCCM compliant** — WM_DELETE_WINDOW, WM_STATE, WM_HINTS, WM_TAKE_FOCUS
- **Focus-follows-mouse** (optional, off by default)
- **Urgent hints** — `_NET_WM_STATE_DEMANDS_ATTENTION` with distinct border colour
- **Fullscreen** — `_NET_WM_STATE_FULLSCREEN` support

---

## Requirements

**Build:**

| Dependency | Arch | Debian/Ubuntu |
|---|---|---|
| GCC or Clang | `gcc` | `gcc` |
| Make | `make` | `make` |
| Xlib | `libx11` | `libx11-dev` |
| Xft (font rendering) | `libxft` | `libxft-dev` |
| Fontconfig | `fontconfig` | `libfontconfig1-dev` |

**One-liner:**

```bash
# Arch
sudo pacman -S gcc make libx11 libxft fontconfig

# Debian / Ubuntu / Kali
sudo apt install gcc make libx11-dev libxft-dev libfontconfig1-dev
```

---

## Build

```bash
cd ~/SwordWM/swordwm
make
```

Debug build (ASan + UBSan):
```bash
make debug
```

Test in a nested X server (safe, no risk to your session):
```bash
make test-xephyr    # needs xorg-server-xephyr / xserver-xephyr
```

Install to `/usr/local/bin`:
```bash
sudo make install
```

---

## Quick Start

**From a TTY (startx):**

```bash
echo 'exec ~/SwordWM/swordwm/launch.sh' >> ~/.xinitrc
startx
```

**From a display manager (LightDM, SDDM, GDM):**

```bash
sudo cp ~/SwordWM/swordwm/swordwm.desktop /usr/share/xsessions/
```

Log out, select **SwordWM** from the session picker, log in.

**First run:** `launch.sh` automatically copies the default config to
`~/.config/swordwm/config` if none exists.

---

## Configuration

Config file: `~/.config/swordwm/config`

Default is installed from `swordwm/config/swordwm.conf` on first launch.

### Settings

```ini
# Terminal emulator
terminal         = ghostty

# Modifier key: super (Win key) | alt | ctrl | shift
mod              = super

# Gaps in pixels
gap_inner        = 8
gap_outer        = 8

# Window decoration
border_width     = 2
title_bar_height = 24

# Focus model
focus_follows_mouse = false

# Colours (hex RGB)
color_focused_border     = #5e81f4
color_unfocused_border   = #2a2d3e
color_focused_title_bg   = #1e2030
color_focused_title_fg   = #c0caf5
color_unfocused_title_bg = #16161e
color_unfocused_title_fg = #565f89
color_urgent_border      = #f7768e
```

### Keybindings

```ini
# Format: bind Mod[+Shift][+Ctrl]+Key = action [arg]
bind Mod+Return      = spawn ghostty
bind Mod+q           = close
bind Mod+j           = focus_next
bind Mod+k           = focus_prev
bind Mod+space       = rotate_layout
bind Mod+Shift+space = toggle_floating
bind Mod+equal       = gap_inc
bind Mod+minus       = gap_dec
bind Mod+Shift+r     = spawn swordwm --reload
bind Mod+Shift+e     = quit

# Workspaces (1-based in config, 0-based internally)
bind Mod+1           = workspace 1
bind Mod+Shift+1     = move_workspace 1
# ... up to 9
```

Available actions: `spawn`, `close`, `focus_next`, `focus_prev`,
`toggle_floating`, `rotate_layout`, `gap_inc`, `gap_dec`,
`quit`, `workspace N`, `move_workspace N`.

Key names follow X11 keysym names: `Return`, `space`, `q`, `j`, `k`,
`1`–`9`, `equal`, `minus`, `e`, `r`, `F1`–`F12`, etc.

### Autostart

```ini
autostart = sworddeck
autostart = ~/SwordWM/swordwm-wallpaper --mode gradient
autostart = picom --daemon
```

### Reload

```bash
# From inside the WM:
Mod+Shift+R

# From a terminal:
swordwm --reload

# Via signal:
kill -HUP $(cat ~/.config/swordwm/swordwm.pid)
```

Changes applied on reload: colours, gaps, keybindings, terminal, focus model.
Title bars redraw immediately. Keys are re-grabbed.

---

## Default Keybindings

| Key | Action |
|---|---|
| `Mod+Return` | Launch terminal (ghostty) |
| `Mod+Q` | Close focused window |
| `Mod+J` | Focus next window |
| `Mod+K` | Focus previous window |
| `Mod+Space` | Rotate layout (tile → monocle → float) |
| `Mod+Shift+Space` | Toggle focused window floating |
| `Mod+Equal` | Increase gaps |
| `Mod+Minus` | Decrease gaps |
| `Mod+Shift+R` | Reload config |
| `Mod+Shift+E` | Quit SwordWM |
| `Mod+1..9` | Switch to workspace 1–9 |
| `Mod+Shift+1..9` | Move window to workspace 1–9 |

Mod = Super (Windows key) by default. Change with `mod = alt` in config.

---

## Project Structure

```
swordwm/
├── Makefile                  # Build system
├── README.md                 # This file
├── BLUEPRINT.md              # Original design spec
├── launch.sh                 # Full session launcher
├── swordwm.desktop           # Display manager session entry
├── config/
│   └── swordwm.conf          # Default config (copied to ~/.config/swordwm/config)
├── include/
│   ├── swordwm.h             # Main header — all includes + function declarations
│   ├── types.h               # Client, Workspace, KeyBinding, WMState structs
│   ├── config.h              # Compile-time defaults + KEYBINDINGS table
│   ├── config_parser.h       # Runtime config (SwordConfig, config_load/reload)
│   ├── decorate.h            # Title bar / decoration functions
│   └── ewmh.h                # EWMH atom and function declarations
└── src/
    ├── main.c                # Entry point, SIGHUP reload, PID file, event loop
    ├── core/
    │   ├── x11.c             # X11 connect, event dispatch, key grab
    │   ├── client.c          # Client list management, focus, framing
    │   └── workspace.c       # Workspace switch, client move
    ├── layout/
    │   └── layout.c          # tile, monocle, float arrange functions
    ├── input/
    │   └── input.c           # Keybind processing + all action_* functions
    ├── decorate/
    │   └── decorate.c        # Xft title bar rendering
    ├── ewmh/
    │   └── ewmh.c            # EWMH/ICCCM property management
    └── config/
        └── config.c          # Runtime config file parser
```

---

## Architecture

```
main()
 ├── config_load()          # parse ~/.config/swordwm/config
 ├── x11_connect()          # XOpenDisplay, root window, atoms, grab keys
 ├── manage_existing_windows()
 ├── config_run_autostart()
 └── event loop
      ├── SIGHUP → config_reload()
      └── XNextEvent → dispatch_event()
           ├── MapRequest    → manage_window() → arrange_workspace()
           ├── UnmapNotify   → unmanage_window() → arrange_workspace()
           ├── KeyPress      → keybind_process()
           │    ├── cfg.binds[] (runtime config, checked first)
           │    └── keybindings[] (compiled-in table, fallback)
           ├── ButtonPress   → focus_client() / drag/resize
           ├── MotionNotify  → move/resize floating window
           ├── Expose        → decorate_draw()
           ├── PropertyNotify → update title, EWMH state
           └── ClientMessage  → _NET_WM_STATE, WM_PROTOCOLS
```

---

## Development

**Xephyr testing** (nested X — safe, won't break your session):

```bash
# Install Xephyr
sudo pacman -S xorg-server-xephyr        # Arch
sudo apt install xserver-xephyr          # Debian/Ubuntu

# Run
make test-xephyr
```

**Memory checking:**
```bash
make debug
DISPLAY=:1 valgrind --leak-check=full ./swordwm
```

**Rebuild and redeploy after changes:**
```bash
make && cp swordwm ~/.local/bin/swordwm
```

---

## Roadmap (Step 12 and beyond)

- [ ] Man page (`man/swordwm.1`)
- [ ] Mouse resize from window edges/corners
- [ ] Multi-monitor support (XRandR)
- [ ] Scratchpad window (toggle hidden terminal)
- [ ] Per-workspace layout config in config file
- [ ] IPC socket (like i3's `i3-msg`) for scripting
- [ ] `docs/keybinds.md` and `docs/architecture.md`
