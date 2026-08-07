# SwordWM — Project Plan & Status

Last updated: 2026-08-07

---

## Components

```
SwordWM/
├── swordwm/          ← Window manager (C11, X11)          ✅ DONE
├── cpp-sworddeck/    ← Desktop overlay (C++17, Qt6)       ✅ DONE
├── cpp-filemanager/  ← File manager (C++17, Qt6)          ✅ DONE
└── src/              ← SwordFish browser (C++17, Qt6/Web) separate repo
```

---

## 1. swordwm — Window Manager

**Status: ✅ Complete and building (zero warnings)**

### What's done
- [x] X11 connection, event loop via `select()` on ConnectionNumber (no CPU spin)
- [x] Master-stack tiling layout with configurable master ratio
- [x] 9 workspaces — switch `Mod+1..9`, move window `Mod+Shift+1..9`
- [x] Floating windows — toggle `Mod+F`, resize/move with mouse
- [x] Title bars — Xft-rendered text, coloured borders (active/inactive/urgent)
- [x] Outer + inner gaps — configurable, inc/dec at runtime (`Mod+=` / `Mod+-`)
- [x] Runtime config reload — `Mod+Shift+R` (no restart)
- [x] Autostart commands in config
- [x] Full ICCCM + EWMH: `_NET_WM_STATE`, `_NET_ACTIVE_WINDOW`, `_NET_WORKAREA`,
      `_NET_CLIENT_LIST`, `_NET_FRAME_EXTENTS`, `_NET_WM_STRUT_PARTIAL`
- [x] `_NET_FRAME_EXTENTS` set on every managed window
- [x] `_NET_WM_STRUT_PARTIAL` / `_NET_WM_STRUT` read from panels/docks → updates workarea
- [x] Config parser: all actions including `reload_config`, `quit`, `gap_inc`, `gap_dec`
- [x] Security: `execv()` array used everywhere (no shell injection via `-c`)
- [x] Makefile: `.obj/` objects, `-MMD -MP` dep tracking, `debug`, `distclean`, `test-xephyr`

### Default keybindings
| Key | Action |
|---|---|
| `Mod+Return` | Terminal (ghostty → alacritty → kitty → xterm) |
| `Mod+Q` | Close focused window |
| `Mod+J` / `Mod+K` | Focus next / previous |
| `Mod+Shift+J` / `Mod+Shift+K` | Move window in stack |
| `Mod+H` / `Mod+L` | Shrink / grow master |
| `Mod+Space` | Rotate layout |
| `Mod+F` | Toggle floating |
| `Mod+Shift+R` | Reload config |
| `Mod+Shift+Q` | Quit |
| `Mod+1..9` | Switch workspace |
| `Mod+Shift+1..9` | Move window to workspace |
| `Mod+=` / `Mod+-` | Increase / decrease gaps |

### Known issues / TODO
- [ ] Multi-monitor (Xinerama/RandR) support
- [ ] Fullscreen hint (`_NET_WM_STATE_FULLSCREEN`) passthrough
- [ ] Per-workspace layout memory

---

## 2. cpp-sworddeck — Desktop Overlay

**Status: ✅ Complete and building (zero warnings)**

Always-on-bottom override-redirect Qt6 window that covers the full desktop.
Three panels: Left+Center (72%) | Right (28%) | Bottom bar.

### Main panel (left+center)

**Always visible:**
- Live clock `HH:MM:SS` at 34pt, updated every second
- Date `ddd dd MMM yyyy` in green

**4 tabs:**

| Tab | What it does |
|---|---|
| 📊 Graph | Renders `~/.config/animated-wallpaper/graph.png`; bottom bar: node/edge count, uptime, kernel; Edit Graph + SwordFM buttons |
| 🌐 Browser | Launches **SwordFish** with `https://duckduckgo.com` as start URL (fallback: zen-browser → firefox → chromium); shows RUNNING / NOT RUNNING + PID + Launch/Kill button |
| >_ Terminal | Launches **ghostty** (fallback: alacritty → kitty → xterm); shows RUNNING/NOT RUNNING + PID + Launch/Kill |
| 📁 Files | Launches **swordfm** (fallback: nautilus → thunar → pcmanfm); same status UI |

> **Design intent:** Browser tab opens DuckDuckGo, not a specific browser binary.
> Terminal tab default is ghostty. Files tab default is swordfm.
> These are the SwordWM defaults — not generic launchers.

### Right panel

**Painted area (updated every 2.5 s):**
- `▸ SWORDDECK` header
- SYSTEM: CPU% + temperature (hwmon), RAM used/total MB, Disk used/total GB — all with segmented LED bars (green/amber/red by load)
- TOP PROCESSES: top 10 by CPU (`ps aux --sort=-%cpu`), NAME / CPU% / MEM% columns
- QUICK KEYS: 6 SwordWM keybind reminders

**Scrollable dock (below stats):**
- APPS: live-search box (Xlib focus grab for override-redirect) + scrollable list — pinned apps from `~/.config/animated-wallpaper/apps.json` first, then all installed apps from 5 XDG `.desktop` dirs (including Flatpak), sorted A-Z
- PANELS: 🌐 Browser / 📁 FM / ⬛ Term buttons — switch main panel tab
- SETTINGS: Edit graph, Audio mixer (pavucontrol), Choose network (nmcli+rofi), Wifi on/off, Mute on/off, Reading mode (redshift 5000K), Audio Visualizer (glava), Restart deck
- COLOR THEME: 6 presets in pairs — Dark · Dracula · Gruvbox · Nord · Solarized · Monokai

### Bottom bar
- Workspace switcher (9 workspaces)
- Live clock
- System tray area

### Source files
```
cpp-sworddeck/src/
├── main.cpp               # Entry point
├── cyberdeck.cpp/h        # Top-level window, X11 hints, layout
├── main_panel.cpp/h       # Left+center: clock, tabs, graph, launchers
├── right_panel.cpp/h      # Right: stats, apps, settings, themes
├── bottom_bar.cpp/h       # Bottom: workspace switcher, clock, tray
├── pipes_layer.cpp/h      # Animated pipe texture (shared background)
└── spectrum_overlay.cpp/h # Audio spectrum visualizer overlay
```

### Known issues / TODO
- [ ] COLOR THEME: accent colour change not yet broadcast to all panels (visual switch only sets button checked state)
- [x] ~~Browser tab: status pill always shows NOT RUNNING~~ — fixed: tracked as QProcess child
- [x] ~~Launch/Kill button had no click handler~~ — fixed: `m_launchBtnRect` stored in `drawLauncherTab`, hit-tested in `mousePressEvent`
- [x] ~~FM tab used `startDetached` so status was always NOT RUNNING~~ — fixed: now uses tracked `QProcess` like Terminal
- [ ] Apps dock: hot-reload when a new .desktop file is installed (currently polls apps.json only)
- [ ] Kill button sends `terminate()` (SIGTERM); if app ignores it, consider `kill()` (SIGKILL) after timeout

---

## 3. cpp-filemanager — File Manager

**Status: ✅ Code complete, needs integration testing**

Qt6 file manager, integrates with SwordDeck's Files tab.

### Features
- Sidebar navigation (bookmarks, devices)
- File preview panel (text, image, PDF)
- Archive operations (zip/tar/7z via archiveops)
- Context menu with "Open With"
- File sharing (shareops)
- Convert operations (convertops)
- Search model with live filter
- Custom file view with icons

### Source layout
```
cpp-filemanager/src/
├── app/
│   ├── main.cpp
│   └── mainwindow.cpp/h
├── panel/
│   ├── sidebar.cpp/h
│   ├── previewpanel.cpp/h
│   ├── toolbar.cpp/h
│   └── statusbar.cpp/h
├── model/
│   ├── filemodel.cpp/h
│   ├── filefilter.cpp/h
│   ├── filescanner.cpp/h
│   └── searchmodel.cpp/h
├── view/
│   └── fileview.cpp/h
└── ops/
    ├── fileops.cpp/h
    ├── archiveops.cpp/h
    ├── contextmenu.cpp/h
    ├── convertops.cpp/h
    ├── openwith.cpp/h
    ├── shareops.cpp/h
    └── termutil.cpp/h
```

---

## 4. swordwm-wallpaper — Animated Wallpaper (C binary)

**Status: ⚠️ Step 1 only — gradient renderer done**

Pre-built binary at `swordwm-wallpaper` (27KB). Sets `_NET_WM_WINDOW_TYPE_DESKTOP`,
always-below, skip-taskbar, sticky.

| Step | Renderer | Status |
|---|---|---|
| 1 | Gradient + floating orbs | ✅ Done |
| 2 | Particles (120 dots, glow) | ❌ TODO |
| 3 | Waves (4 layered sine) | ❌ TODO |
| 4 | Matrix (katakana rain, Pango) | ❌ TODO |
| 5 | Graph/audio visualizer (libpulse) | ❌ TODO |
| 6 | CLI polish (--next, --stop, PID file) | ❌ TODO |
| 7 | Config file | ❌ TODO |
| 8 | Wayland stub | ❌ TODO |

---

## 5. SwordFish Browser

**Status: Separate repo — not built here**

See: https://github.com/BayazidHabibSiddikee/SwordFish/releases

The `src/` directory at repo root contains the browser source but it is a
separate project with its own CMakeLists.txt.

---

## Build commands

```bash
# Window manager
cd swordwm && make                    # build
cd swordwm && make debug              # ASAN + debug build
cd swordwm && make test-xephyr        # test in nested X server
sudo make -C swordwm install          # install to /usr/local/bin

# Desktop overlay
cd cpp-sworddeck
cmake -B build && cmake --build build --parallel

# File manager
cd cpp-filemanager
cmake -B build && cmake --build build --parallel
```

## Dependencies

```bash
# swordwm (Debian/Ubuntu)
sudo apt install libx11-dev libxft-dev libfontconfig1-dev pkg-config

# swordwm (Arch)
sudo pacman -S libx11 libxft fontconfig pkg-config

# sworddeck + filemanager (Debian/Ubuntu)
sudo apt install qt6-base-dev libqt6x11extras5-dev libx11-dev cmake g++

# sworddeck + filemanager (Arch)
sudo pacman -S qt6-base libx11 cmake gcc
```

---

## Config file (`~/.config/swordwm/config`)

Copied from `swordwm/config/swordwm.conf` on first launch.

Key options:
```ini
gap_outer        = 8
gap_inner        = 4
border_width     = 2
title_bar_height = 20
border_active    = #61afef
border_inactive  = #3e4451
border_urgent    = #e06c75

bind Mod+Return       = spawn ghostty
bind Mod+Shift+Return = spawn rofi -show run
bind Mod+Shift+r      = reload_config
bind Mod+Shift+q      = quit

autostart = picom --daemon
autostart = sworddeck &
```

---

## Design decisions to remember

- **Browser tab launches SwordFish** with `https://duckduckgo.com` as the start URL, tracked as a child process (RUNNING/NOT RUNNING status pill). Falls back to zen-browser → firefox → chromium if SwordFish is not installed. SwordFish binary is at `~/.local/bin/SwordFish`.
- **Terminal default is ghostty** — it's the SwordWM default terminal, not generic.
- **Files default is swordfm** — the project's own file manager, not nautilus.
- **App launcher in right panel** scans all 5 XDG `.desktop` dirs including Flatpak exports. Pinned apps (apps.json) always appear first.
- **Config reload is instant** (`reload_config` action) — no WM restart needed.
- **Strut support**: panels/docks with `_NET_WM_STRUT_PARTIAL` automatically shrink the tiling area. SwordDeck's bottom bar should set its strut.
- **No shell injection**: all `execv()` calls use argv arrays, never `-c "string"`.
