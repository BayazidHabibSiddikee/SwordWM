# 🗡️ SwordWM

A minimal, tiling **X11 window manager** written in C, paired with a cyberpunk-aesthetic **desktop overlay** (SwordDeck) written in C++/Qt6.

---

## 📐 Architecture overview

```
SwordWM/
├── swordwm/          ← The window manager (C, X11)
├── cpp-sworddeck/    ← Desktop overlay / status panel (C++, Qt6)
├── cpp-filemanager/  ← File manager (C++, Qt6)
└── src/              ← SwordFish browser components (C++, Qt6/WebEngine)
```

The three main components are independent — you can run SwordWM without SwordDeck, or SwordDeck on top of any other WM.

---

## 🪟 SwordWM — Window Manager

**Location:** `swordwm/`
**Language:** C11, X11/Xlib, Xft, fontconfig
**Build:** `make` (GNU Make + pkg-config)

### Features

| Feature | Details |
|---|---|
| Tiling layouts | Master–stack with configurable master ratio; rotate through layouts with `Mod+Space` |
| 9 workspaces | Switch with `Mod+1..9`, move window with `Mod+Shift+1..9` |
| Floating windows | Toggle with `Mod+F`; resize/move with mouse |
| Title bars | Xft-rendered title + coloured border; active/inactive/urgent colours |
| Gaps | Configurable outer and inner gaps, inc/dec at runtime |
| ICCCM/EWMH | Full `_NET_WM_*` support — panels, docks, taskbars work correctly |
| Strut support | `_NET_WM_STRUT_PARTIAL` — bars/docks reserve their space automatically |
| Frame extents | `_NET_FRAME_EXTENTS` set on every managed window |
| Config reload | `Mod+Shift+R` — instant reload, no restart needed |
| Autostart | Run arbitrary commands on startup via config |
| DNS-over-HTTPS | n/a — handled at the browser level (SwordFish) |

### Keybindings (defaults)

| Key | Action |
|---|---|
| `Mod+Return` | Terminal (ghostty → alacritty → kitty → xterm) |
| `Mod+Q` | Close focused window |
| `Mod+J` / `Mod+K` | Focus next / previous window |
| `Mod+Shift+J` / `Mod+Shift+K` | Move window in stack |
| `Mod+H` / `Mod+L` | Shrink / grow master area |
| `Mod+Space` | Rotate layout |
| `Mod+F` | Toggle floating |
| `Mod+Shift+R` | Reload config (instant) |
| `Mod+Shift+Q` | Quit SwordWM |
| `Mod+1..9` | Switch workspace |
| `Mod+Shift+1..9` | Move window to workspace |
| `Mod+=` / `Mod+-` | Increase / decrease gaps |

> `Mod` defaults to the Super key. Edit `~/.config/swordwm/config` to change anything.

### Source layout

```
swordwm/
├── src/
│   ├── main.c               # Entry point; select() event loop (no CPU spin)
│   ├── core/
│   │   ├── x11.c            # X11 connection, event dispatch
│   │   ├── client.c         # Window tracking, manage/unmanage
│   │   └── workspace.c      # Workspace (virtual desktop) management
│   ├── layout/
│   │   └── layout.c         # Master-stack tiling algorithm
│   ├── input/
│   │   └── input.c          # Key/mouse binding parser and dispatcher
│   ├── decorate/
│   │   └── decorate.c       # Title bar, border drawing (Xft)
│   ├── ewmh/
│   │   └── ewmh.c           # ICCCM + EWMH property handling
│   └── config/
│       └── config.c         # Runtime config file parser + reload
├── include/
│   ├── swordwm.h            # Shared types and includes
│   ├── config.h             # Compile-time defaults
│   ├── config_parser.h      # Config parser API
│   ├── ewmh.h               # EWMH function declarations
│   └── types.h              # Client, Workspace, WM structs
├── config/
│   └── swordwm.conf         # Default config (copied to ~/.config/swordwm/config)
├── Makefile                 # Build system
├── launch.sh                # Session launcher (for ~/.xinitrc or a DM)
└── swordwm.desktop          # XSession desktop entry
```

### Build

```bash
# Dependencies (Debian/Ubuntu)
sudo apt install libx11-dev libxft-dev libfontconfig1-dev

# Dependencies (Arch)
sudo pacman -S libx11 libxft fontconfig

# Build
cd swordwm
make

# Install
sudo make install          # installs to /usr/local/bin/swordwm
```

### Session setup

```bash
# Option 1 — startx
echo 'exec ~/SwordWM/swordwm/launch.sh' > ~/.xinitrc
startx

# Option 2 — display manager
sudo cp swordwm/swordwm.desktop /usr/share/xsessions/
# Select "SwordWM" from your DM's session list
```

The `launch.sh` script:
- Copies the default config to `~/.config/swordwm/config` if not present
- Sets keyboard repeat rate and disables screen blanking
- Optionally starts `swordwm-wallpaper` and `sworddeck` if the binaries exist
- `exec`s the WM last (the WM is the session's lifetime process)

### Config file

The config lives at `~/.config/swordwm/config` (copied from `swordwm/config/swordwm.conf` on first launch). Example entries:

```ini
# Gaps (pixels)
gap_outer = 8
gap_inner = 4

# Appearance
border_width     = 2
title_bar_height = 20
border_active    = #61afef
border_inactive  = #3e4451
border_urgent    = #e06c75

# Keybindings
bind Mod+Return       = spawn ghostty
bind Mod+Shift+Return = spawn rofi -show run
bind Mod+Shift+r      = reload_config
bind Mod+Shift+q      = quit

# Autostart
autostart = picom --daemon
autostart = sworddeck &
```

---

## 🖥️ SwordDeck — Desktop Overlay

**Location:** `cpp-sworddeck/`
**Language:** C++17, Qt6 (Widgets, X11)
**Build:** CMake

SwordDeck is an always-on-bottom override-redirect desktop overlay — it draws on the root window area without being managed by the WM. It is divided into three panels:

```
┌─────────────────────────────────────────────────────────┐
│  Left+Center panel (28%+44%)  │  Right panel (28%)      │
│                               │                          │
│  [📊 Graph][🌐 Browser]       │  ▸ SWORDDECK             │
│  [>_ Terminal][📁 Files]      │                          │
│                               │  ── SYSTEM ──            │
│  HH:MM:SS                     │  CPU  42%  51°C  ████▌   │
│  Fri 07 Aug 2026              │  RAM  3.1G/16G   ██▌     │
│                               │  DISK 120G/512G  ██▌     │
│  [tab content]                │                          │
│                               │  ── TOP PROCESSES ──     │
│                               │  NAME       CPU%  MEM%   │
│                               │  firefox    12.3  4.1    │
│                               │  ...                     │
│                               │                          │
│                               │  ── QUICK KEYS ──        │
│                               │  Mod+Return  » terminal  │
│                               │  ...                     │
│                               │                          │
│                               │  ┌──────────────────┐   │
│                               │  │ ── APPS ──  🔍   │   │
│                               │  │ ▸ Terminal        │   │
│                               │  │ ▸ Browser         │   │
│                               │  │ ▸ Files           │   │
│                               │  │ ▸ <all .desktop>  │   │
│                               │  │ ── PANELS ──      │   │
│                               │  │ 🌐Browser 📁FM ⬛  │   │
│                               │  │ ── SETTINGS ──    │   │
│                               │  │ Edit graph        │   │
│                               │  │ Audio mixer       │   │
│                               │  │ Brightness        │   │
│                               │  │ Choose network…   │   │
│                               │  │ Wifi on/off       │   │
│                               │  │ Mute on/off       │   │
│                               │  │ Reading mode      │   │
│                               │  │ Audio Visualizer  │   │
│                               │  │ Restart deck      │   │
│                               │  │ Color theme:      │   │
│                               │  │ [Dark] [Dracula]  │   │
│                               │  │ [Gruvbox][Nord]   │   │
│                               │  │ [Solar][Monokai]  │   │
│                               │  └──────────────────┘   │
└─────────────────────────────────────────────────────────┘
            BottomBar (clock, workspace, system tray)
```

### Right panel — what's in it

**Painted area (top, updated every 2.5 s):**

| Section | Content |
|---|---|
| Header | `▸ SWORDDECK` title centred |
| SYSTEM | CPU% + temperature (hwmon), segmented LED bar |
| SYSTEM | RAM used/total MB + %, segmented LED bar |
| SYSTEM | Disk used/total GB + %, segmented LED bar |
| TOP PROCESSES | Top 10 processes by CPU (`ps aux --sort=-%cpu`), columns: NAME / CPU% / MEM% |
| QUICK KEYS | 6 SwordWM keybind reminders |

**Dock area (below painted stats, fully scrollable QScrollArea):**

| Section | Content |
|---|---|
| APPS | Search box (live filter, Xlib focus grab for override-redirect) + scrollable list of all installed GUI apps; pinned apps from `~/.config/animated-wallpaper/apps.json` appear first, then every app found in the 5 standard XDG `.desktop` dirs (including Flatpak exports), sorted A-Z |
| PANELS | Three buttons — 🌐 Browser / 📁 FM / ⬛ Term — that switch the main panel's active tab |
| SETTINGS | Edit graph · Audio mixer (pavucontrol) · Brightness (brightnessctl) · Choose network (nmcli+rofi) · Wifi on/off · Mute on/off · Reading mode toggle (redshift 5000K) · Audio Visualizer toggle (glava) · Restart deck |
| COLOR THEME | 6 accent presets: Dark (default) · Dracula · Gruvbox · Nord · Solarized · Monokai — displayed as checkable buttons in pairs, each styled with its own accent colour |

### Main panel — what's in it

**Always visible (top):**
- Live clock: `HH:MM:SS` in 34pt, updated every second
- Date: `ddd dd MMM yyyy`

**Tab bar (4 tabs):**

| Tab | Content |
|---|---|
| 📊 Graph | Animated graph PNG from `~/.config/animated-wallpaper/graph.png`; bottom status bar shows node/edge count, uptime, kernel version. Edit Graph + SwordFM buttons in bottom-right corner |
| 🌐 Browser | Launches SwordFish (falls back to zen-browser → firefox → chromium); shows RUNNING / NOT RUNNING status pill + PID; Launch/Kill button |
| >_ Terminal | Launches ghostty (falls back to alacritty → kitty → xterm); same status UI |
| 📁 Files | Launches swordfm (falls back to nautilus → thunar → pcmanfm); same status UI |

### Build

```bash
# Dependencies (Debian/Ubuntu)
sudo apt install qt6-base-dev libqt6x11extras5-dev libx11-dev

# Dependencies (Arch)
sudo pacman -S qt6-base libx11

# Build
cd cpp-sworddeck
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

---

## 📁 SwordFM — File Manager

**Location:** `cpp-filemanager/`
**Language:** C++17, Qt6
**Build:** CMake

A Qt6 file manager that integrates with SwordDeck's Files tab. Features sidebar navigation, file preview panel, archive operations, context menu with "Open With", and file sharing.

---

## 🌐 SwordFish — Browser (legacy / separate repo)

The `src/` directory at the root contains the **SwordFish** browser source (C++, Qt6/WebEngine). SwordFish is a separate project with its own build system. Refer to the [SwordFish releases page](https://github.com/BayazidHabibSiddikee/SwordFish/releases) for the standalone browser.

---

## 🔧 Build requirements summary

| Component | Language | Build tool | Key deps |
|---|---|---|---|
| swordwm | C11 | GNU Make | libx11, libxft, libfontconfig |
| cpp-sworddeck | C++17 | CMake | Qt6 Widgets, libx11 |
| cpp-filemanager | C++17 | CMake | Qt6 Widgets |
| SwordFish browser | C++17 | CMake | Qt6 WebEngine, libcurl |

One-liner for all build deps on Debian/Ubuntu:
```bash
sudo apt install \
  libx11-dev libxft-dev libfontconfig1-dev \
  qt6-base-dev libqt6x11extras5-dev \
  cmake g++ ninja-build pkg-config
```

---

## 📁 Project layout

```
SwordWM/
├── swordwm/              # Window manager (C)
│   ├── src/              # Source: core, layout, input, decorate, ewmh, config
│   ├── include/          # Headers
│   ├── config/           # Default swordwm.conf
│   ├── Makefile
│   └── launch.sh         # Session launcher
├── cpp-sworddeck/        # Desktop overlay (C++/Qt6)
│   └── src/
│       ├── main.cpp
│       ├── cyberdeck.cpp/h     # Top-level window, layout
│       ├── main_panel.cpp/h    # Left+center: clock, tabs, graph, launchers
│       ├── right_panel.cpp/h   # Right: stats, apps, settings, themes
│       ├── bottom_bar.cpp/h    # Bottom: workspace switcher, clock, tray
│       ├── pipes_layer.cpp/h   # Animated pipe texture (shared background)
│       └── spectrum_overlay.cpp/h  # Audio spectrum visualizer overlay
├── cpp-filemanager/      # File manager (C++/Qt6)
├── src/                  # SwordFish browser source
├── tools/                # Python tool scripts (PDF, office, translate, etc.)
├── utils/                # Shared utilities (adblocker, proxy, network)
├── screenshots/
├── launch.sh             # Top-level session launcher
└── cyberdesk.sh          # Helper script (glava-toggle, restart, etc.)
```

---

## 🤝 Contributing

```bash
git clone https://github.com/BayazidHabibSiddikee/SwordWM.git
cd SwordWM

# Build the WM
cd swordwm && make && cd ..

# Build the deck
cd cpp-sworddeck && mkdir -p build && cmake -B build && cmake --build build --parallel && cd ..

# Test in a nested X server
cd swordwm && make test-xephyr
```

---

## 📄 License

See [LICENSE](LICENSE).
