# SwordWM — Project Map & Status

> Status legend: ✅ Done | 🔧 In Progress | ⚠️ Known Issue | ❌ Broken

---

## Part 1 — X11 Foundation
**Files:** `src/main.c`, `src/core/x11.c`, `src/core/client.c`, `src/core/workspace.c`

- **1.1** X11 Connection — XOpenDisplay, atoms, root window ✅
- **1.2** Event Loop — pselect()-based with signal handling ✅
- **1.3** Window Tracking — Client list, manage/unmanage ✅
- **1.4** Workspace Management — 9 virtual desktops, switch/move ✅
- **1.5** Signal Handling — SIGHUP reload, SIGCHLD reaper ✅
- **1.6** Error Handling — startup + runtime X error handlers ✅

---

## Part 2 — Layout Engine
**Files:** `src/layout/layout.c`

- **2.1** Master/Stack Tiling — configurable ratio ✅
- **2.2** Monocle Layout — fullscreen stacking ✅
- **2.3** Floating Mode — free positioning, no auto-tile ✅
- **2.4** Gap System — inner/outer gaps, runtime adjustable ✅
- **2.5** Layout Cycling — tile → monocle → float ✅

---

## Part 3 — Input & Focus
**Files:** `src/input/input.c`

- **3.1** Keyboard Shortcuts — configurable via config file ✅
- **3.2** Click-to-Focus — border color feedback ✅
- **3.3** Focus Cycling — Mod+j/k next/prev ✅
- **3.4** Stack Reordering — Mod+Shift+j/k swap ✅
- **3.5** Master Resize — Mod+h/l grow/shrink ✅

---

## Part 4 — Window Decorations
**Files:** `src/decorate/decorate.c`

- **4.1** Title Bar — window name, focused/unfocused colors ✅
- **4.2** Close Button — click to close ✅
- **4.3** Drag to Move — title bar drag ✅
- **4.4** Edge/Corner Resize — 8-direction resize handles ✅
- **4.5** Velocity Tracking — measures drag speed for physics ✅

---

## Part 5 — EWMH & ICCCM
**Files:** `src/ewmh/ewmh.c`

- **5.1** _NET_SUPPORTED — advertise supported properties ✅
- **5.2** _NET_CLIENT_LIST — managed window list ✅
- **5.3** _NET_ACTIVE_WINDOW — focused window ✅
- **5.4** _NET_WM_DESKTOP — workspace assignment ✅
- **5.5** _NET_WORKINGAREA — usable area ✅
- **5.6** Fullscreen Support — _NET_WM_STATE_FULLSCREEN ✅
- **5.7** Urgency Hints — _NET_WM_STATE_DEMANDS_ATTENTION ✅
- **5.8** Strut Support — panel/dock reserved space ✅

---

## Part 6 — Configuration
**Files:** `src/config/config.c`, `config/swordwm.conf`

- **6.1** Config File Parser — key=value format ✅
- **6.2** Runtime Reload — SIGHUP or Mod+Shift+r ✅
- **6.3** Custom Keybindings — bind lines in config ✅
- **6.4** Autostart Commands — runs once at WM start ✅
- **6.5** Color Configuration — hex RGB values ✅
- **6.6** Font Configuration — Xft font spec ✅

---

## Part 7 — Physics System (fwm + SwordWM hybrid)
**Files:** `src/physics.c`, `include/physics.h`

> Combines fwm's rigid-body concepts with X11 spring model.

### 7.1 Core Physics Engine ✅
- Semi-implicit Euler integration (stable, simple)
- Sub-stepping for high-speed stability (up to 8 substeps)
- Speed clamping to prevent tunneling

### 7.2 Gravity & Mass ✅
- Configurable gravity (px/s^2, default 981 = earth-like)
- Per-workspace gravity profiles
- Window mass derived from area (heavier = harder to push)
- Per-body gravity scale multiplier

### 7.3 Collision System ✅
- AABB overlap detection between windows
- Impulse-based collision response
- Window-to-window collision on same workspace
- Configurable restitution (bounciness)
- Collision filtering (tiled/floating/pinned windows skip)

### 7.4 Wall Physics ✅
- Screen edge bouncing with restitution
- Velocity reflection on impact
- Configurable wall bounce factor

### 7.5 Throwing & Momentum ✅
- Throw windows with mouse velocity
- Configurable throw speed multiplier
- Maximum throw speed cap
- Momentum conservation after release

### 7.6 Spring Wobble Animation ✅
- Map-in bounce (pop from smaller with overshoot)
- Drop bounce with velocity tracking
- Squash/stretch on fast movement
- Corner lag (jelly-like deformation)
- Wave propagation through window body
- Multi-bounce settling (up to 4 bounces)
- Edge bounce off screen boundaries
- Inertia after release with friction

### 7.7 Per-Workspace Profiles ✅
- Independent gravity, friction, restitution per workspace
- Config via `physics_*` config options
- Runtime adjustable

---

## Part 8 — Build & Install
**Files:** `Makefile`, `config/swordwm.conf`

| Target | Command | Status |
|---|---|---|
| Build | `make` | ✅ |
| Clean | `make clean` | ✅ |
| Debug | `make debug` (ASAN+UBSAN) | ✅ |
| Install | `make install` | ✅ |
| Xephyr Test | `make test-xephyr` | ✅ |

---

## Configuration Reference

```bash
# Physics settings
physics_enabled     = true        # enable/disable window physics
physics_gravity     = 981.0       # px/s^2 (981=earth, 0=zero-g)
physics_friction    = 0.985       # velocity retention (0-1)
physics_restitution = 0.3         # bounce factor (0-1)
physics_mass_density = 0.0005     # mass per px^2
physics_throw_mult  = 0.65        # throw speed multiplier
physics_max_throw   = 1800.0      # max throw speed px/s
```

---

## File Structure

```
swordwm/
├── Makefile
├── BLUEPRINT.md
├── README.md
├── config/
│   └── swordwm.conf           # Default config
├── include/
│   ├── swordwm.h              # Main header
│   ├── types.h                # Core types (Client, Workspace, WMState)
│   ├── config.h               # Compile-time defaults
│   ├── config_parser.h        # Runtime config API
│   ├── decorate.h             # Decoration functions
│   ├── ewmh.h                 # EWMH functions
│   └── physics.h              # Physics world, bodies, animation
├── src/
│   ├── main.c                 # Entry point, event loop
│   ├── physics.c              # Physics engine (~950 lines)
│   ├── core/
│   │   ├── x11.c              # X11 connection, events, atoms
│   │   ├── client.c           # Client list, frame creation
│   │   └── workspace.c        # Workspace management
│   ├── layout/
│   │   └── layout.c           # Tile, monocle, floating
│   ├── input/
│   │   └── input.c            # Keyboard shortcuts, actions
│   ├── decorate/
│   │   └── decorate.c         # Title bar, drag, resize
│   ├── ewmh/
│   │   └── ewmh.c             # EWMH properties
│   └── config/
│       └── config.c           # Config file parser
```

---

## Architecture

```
┌─────────────────────────────────────────────┐
│              Main Event Loop                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│  │  Events  │→ │ Dispatch │→ │ Handlers │  │
│  └──────────┘  └──────────┘  └──────────┘  │
│                                            │
│  ┌──────────────────────────────────────┐   │
│  │         Window Manager Core          │   │
│  │  ┌────────┐ ┌────────┐ ┌─────────┐  │   │
│  │  │ Window │ │ Layout │ │  Key    │  │   │
│  │  │ List   │ │ Engine │ │ Bindings│  │   │
│  │  └────────┘ └────────┘ └─────────┘  │   │
│  │  ┌────────┐ ┌────────┐ ┌─────────┐  │   │
│  │  │ Focus  │ │Decorate│ │Desktops │  │   │
│  │  │ Manager│ │ Engine │ │ Manager │  │   │
│  │  └────────┘ └────────┘ └─────────┘  │   │
│  │  ┌────────────────────────────────┐  │   │
│  │  │       Physics Engine           │  │   │
│  │  │  Gravity, Collisions, Wobble   │  │   │
│  │  └────────────────────────────────┘  │   │
│  └──────────────────────────────────────┘   │
│                                            │
│  ┌──────────────────────────────────────┐   │
│  │         X11 / Xlib Layer             │   │
│  │  Connection, Events, Atoms, Hints    │   │
│  └──────────────────────────────────────┘   │
└─────────────────────────────────────────────┘
```

---

## Dependencies

| Library | Purpose | Required |
|---|---|---|
| libX11 | X11 client library | Yes |
| libXft | Font rendering (Xft) | Yes |
| fontconfig | Font discovery | Yes |
| libm | Math (hypot, sqrt, etc.) | Yes |

---

## Known Issues / Next Steps

| ID | Description | Status |
|----|-------------|--------|
| P1 | Physics runs at event loop rate (~60fps), not independent tick | ⚠️ by design |
| P2 | Window-to-window collision only on same workspace | ⚠️ by design |
| P3 | Old wobble.c still in source tree (unused, not compiled) | ⚠️ cleanup needed |
| P4 | No per-window physics rules (rule_mass, rule_gravity etc.) | 🔧 future |
| P5 | No audio visualizer bar physics (from fwm) | 🔧 future |
