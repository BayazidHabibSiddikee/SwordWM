# SwordWM Animated Wallpaper — Python to C Conversion

**Objective:** Convert the Python/PySide6 animated wallpaper to pure C using X11 (Xlib) and Cairo for rendering. Architecture ready for Wayland via abstraction layer.

**Language:** C (C11)  
**Display:** X11 (Xlib) primary, Wayland-ready abstraction  
**Rendering:** Cairo (2D graphics library — cross-platform, works on both X11 and Wayland)  
**Audio:** PulseAudio simple API (for graph visualizer)

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                    Main Loop                         │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────┐ │
│  │  Event Loop  │  │  Render Loop │  │ Audio In  │ │
│  │  (X11/Way)  │  │  (Cairo/60fps)│  │ (Pulse)   │ │
│  └──────┬───────┘  └──────┬───────┘  └─────┬─────┘ │
│         │                 │                │        │
│  ┌──────▼─────────────────▼────────────────▼─────┐  │
│  │              Abstraction Layer                │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────────┐  │  │
│  │  │ Display  │ │ Surface  │ │ Input/Audio  │  │  │
│  │  │ (X11/W) │ │(Cairo)   │ │              │  │  │
│  │  └──────────┘ └──────────┘ └──────────────┘  │  │
│  └───────────────────────────────────────────────┘  │
│                                                     │
│  ┌───────────────────────────────────────────────┐  │
│  │              Renderers                        │  │
│  │  gradient │ particles │ wave │ matrix │ graph  │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

---

## Why C + Cairo (not raw Xlib drawing)

| Approach | Pros | Cons |
|----------|------|------|
| **Raw Xlib (XDrawLine, etc)** | No deps, fast | Primitive API, no anti-aliasing, no gradients, X11-only |
| **Cairo + Xlib backend** | Beautiful rendering, anti-aliased, gradients, works on Wayland too | One dependency (libcairo) |
| **OpenGL** | GPU-accelerated, flashy | Complex setup, overkill for 2D |
| **Cairo + Wayland backend** | Same code, Wayland support | Needs libcairo (already common) |

**Decision: Cairo** — it renders to any backend (X11, Wayland, image, PDF). Write renderer once, works everywhere.

---

## File Structure

```
swordwm-wallpaper/
├── Makefile
├── README.md
├── include/
│   ├── ww.h              # Main header, types
│   ├── display.h          # Display abstraction (X11/Wayland)
│   ├── renderer.h         # Renderer interface
│   ├── audio.h            # Audio capture
│   └── renderers/
│       ├── gradient.h
│       ├── particles.h
│       ├── wave.h
│       ├── matrix.h
│       └── graph.h
├── src/
│   ├── main.c             # Entry point, arg parsing
│   ├── display_x11.c      # X11 display backend
│   ├── display_wayland.c  # Wayland display backend (stub/phase 2)
│   ├── renderer.c         # Renderer dispatch
│   ├── audio_pulse.c      # PulseAudio capture
│   ├── audio_fallback.c   # Sine wave fallback
│   └── renderers/
│       ├── gradient.c
│       ├── particles.c
│       ├── wave.c
│       ├── matrix.c
│       └── graph.c
└── config/
    └── swordwm-wallpaper.conf
```

---

## Phase 1: X11 Foundation (Steps 1–3)

### Step 1: Project Scaffold & X11 Window
**Depends on:** None  
**Estimated effort:** Small

**Context:** Create project structure, Makefile, and a minimal X11 window that fills the screen and sets desktop properties.

**Task list:**
- Create directory structure
- Write `Makefile` with: `CC=gcc`, `CFLAGS=-Wall -Wextra -O2 $(shell pkg-config --cflags cairo x11)`, `LDFLAGS=$(shell pkg-config --libs cairo x11)`
- Implement `display_x11.c`:
  - `display_init()` — XOpenDisplay, get root window, get screen geometry
  - `display_create_window()` — XCreateSimpleWindow fullscreen, override_redirect, event masks
  - `display_set_desktop_hints()` — _NET_WM_WINDOW_TYPE_DESKTOP, _NET_WM_STATE_BELOW, _NET_WM_SKIP_TASKBAR/PAGER, _NET_WM_DESKTOP=0xFFFFFFFF
  - `display_destroy()` — XDestroyWindow, XCloseDisplay
- Implement `main.c` — parse args (--mode gradient|particles|wave|matrix|graph), init display, create window, enter event loop
- Event loop: XNextEvent, handle Expose (redraw), ConfigureNotify (resize), ClientMessage (WM protocols)

**Verification:**
```bash
make && ./swordwm-wallpaper --mode gradient
# Should show fullscreen dark window, set as desktop
# xprop should show _NET_WM_WINDOW_TYPE_DESKTOP
```

**Exit criteria:** Fullscreen window appears, is set as desktop, handles resize.

---

### Step 2: Cairo Rendering Surface
**Depends on:** Step 1  
**Estimated effort:** Small

**Context:** Wrap Cairo surface creation around the X11 window. This gives us a cross-platform rendering API.

**Task list:**
- Implement `display_get_cairo_surface()` — create Cairo XLib surface from X11 window
- Handle surface recreation on ConfigureNotify (window resize)
- Add double buffering: create Cairo image surface, render to it, then paint to X11 surface
- Implement `display_flush()` — cairo_surface_flush + XFlush
- Add frame timing: target 60fps with clock_nanosleep

**Verification:**
```bash
# Draw a solid color fill — should fill entire screen
# Resize terminal → window resizes, fills correctly
```

**Exit criteria:** Cairo renders to X11 window, handles resize, runs at 60fps.

---

### Step 3: Renderer Interface & Gradient
**Depends on:** Step 2  
**Estimated effort:** Small

**Context:** Define the renderer interface and implement the first renderer (gradient) to prove the pipeline works end-to-end.

**Task list:**
- Define `Renderer` interface in `renderer.h`:
  ```c
  typedef struct {
      const char *name;
      void (*init)(int width, int height);
      void (*render)(cairo_t *cr, int width, int height, double time);
      void (*destroy)(void);
  } Renderer;
  ```
- Implement `gradient.c`:
  - Slowly rotating hue
  - Radial gradient center
  - 6 floating orbs with pulsing alpha
  - HSV color generation: `hsv_to_rgb()` helper
- Wire into main loop: select renderer by name, call render() each frame

**Verification:**
```bash
./swordwm-wallpaper --mode gradient
# Should show animated gradient with floating orbs
```

**Exit criteria:** Gradient animation renders smoothly at 60fps.

---

## Phase 2: All Renderers (Steps 4–7)

### Step 4: Particles Renderer
**Depends on:** Step 3  
**Estimated effort:** Small

**Task list:**
- Implement `particles.c` — 120 particles with:
  - Position (x, y), velocity (vx, vy)
  - Size, alpha, hue, pulse speed
  - Wrap around screen edges
  - Draw: inner filled circle + outer glow circle
  - Background: dark (10, 10, 18)
- Particle struct array, updated each frame

**Verification:** Particles float, pulse, wrap at edges.

---

### Step 5: Wave Renderer
**Depends on:** Step 3  
**Estimated effort:** Small

**Task list:**
- Implement `wave.c` — 4 layered sine waves:
  - Each layer: y_offset + amplitude * sin(x * freq + t * speed)
  - Secondary harmonic for complexity
  - Fill below wave with semi-transparent HSV color
  - Stroke wave line on top
  - Background: dark (8, 8, 16)

**Verification:** Waves undulate smoothly, layers overlap with transparency.

---

### Step 6: Matrix Renderer
**Depends on:** Step 3  
**Estimated effort:** Medium

**Task list:**
- Implement `matrix.c` — falling katakana columns:
  - Array of columns, each with: x, y, speed, character buffer, length, brightness
  - Characters: Unicode katakana (U+30A0–U+30FF)
  - Head character: bright white-green
  - Trail: fading green alpha
  - Random character mutations (~2% chance per frame)
  - Need: Pango for text rendering (or Xft)
  - Background: black

**Note:** Text rendering needs Pango or Xft. Use Pango (works with Cairo natively).

**Add to Makefile:** `pkg-config --cflags --libs pango pangocairo`

**Verification:** Katakana rain falls, characters mutate, trail fades.

---

### Step 7: Graph (Audio Visualizer) Renderer
**Depends on:** Step 3  
**Estimated effort:** Medium

**Task list:**
- Implement `audio_pulse.c`:
  - `audio_init()` — pa_simple_new for PulseAudio recording
  - `audio_read(float *buf, int n)` — read n float samples
  - Smooth: `smooth[i] = smooth[i] * 0.75 + raw[i] * 0.25`
- Implement `audio_fallback.c`:
  - Generate sine waves when PulseAudio unavailable
  - `sin(t * 2 + i * 0.3) * 0.3 + 0.3`
- Implement `graph.c` — 48 bar visualizer:
  - Bar width based on screen width
  - Height from audio amplitude * 0.6
  - Gradient fill per bar (HSV)
  - Reflection below
  - Glow dot on top
  - Scanline overlay
  - Background: dark (8, 8, 14)

**Verification:** Bars react to system audio (or sine fallback).

---

## Phase 3: Polish (Steps 8–9)

### Step 8: Command Line & Config
**Depends on:** Step 7  
**Estimated effort:** Small

**Task list:**
- Full argument parsing: `--mode`, `--list`, `--stop`, `--help`
- State file: `~/.config/swordwm-wallpaper/state` (save/restore last mode)
- PID file: `~/.config/swordwm-wallpaper/pid` (for --stop)
- Config file: `~/.config/swordwm-wallpaper/config`
  - `mode=gradient`
  - `fps=60`
  - `audio_device=default`
- Signal handling: SIGTERM/SIGINT for clean exit, SIGHUP for reload

**Verification:** `--stop` kills running instance, `--next` cycles modes.

---

### Step 9: Wayland Stub & Documentation
**Depends on:** Step 8  
**Estimated effort:** Small

**Task list:**
- Create `display_wayland.c` — stub that prints "Wayland support coming soon"
- Build system: conditionally compile X11 or Wayland backend
  ```makefile
  ifneq ($(WAYLAND),1)
    SRC += src/display_x11.c
    LIBS += $(shell pkg-config --libs x11)
  else
    SRC += src/display_wayland.c
    LIBS += $(shell pkg-config --libs wayland-client)
  endif
  ```
- Write `README.md` with build instructions, usage, modes
- Man page: `man/swordwm-wallpaper.1`

**Verification:**
```bash
make                    # Builds X11 version
make WAYLAND=1          # Builds Wayland stub
./swordwm-wallpaper --help
```

---

## Dependencies

| Library | Purpose | Install |
|---------|---------|---------|
| libcairo2-dev | 2D rendering | `sudo apt install libcairo2-dev` |
| libx11-dev | X11 connection | `sudo apt install libx11-dev` |
| libpango1.0-dev | Text rendering (matrix) | `sudo apt install libpango1.0-dev` |
| libpulse-dev | Audio capture (graph) | `sudo apt install libpulse-dev` |

**One-liner:**
```bash
sudo apt install libcairo2-dev libx11-dev libpango1.0-dev libpulse-dev
```

---

## Key Differences from Python Version

| Feature | Python (PySide6) | C (Cairo) |
|---------|-------------------|-----------|
| Window creation | Qt handles it | Xlib directly |
| Rendering | QPainter | Cairo |
| Text (matrix) | QFont | Pango + Cairo |
| Audio | subprocess + parec | libpulse directly |
| Desktop hints | python-xlib | Xlib directly |
| Memory | GC'd | Manual (but tiny footprint) |
| Binary size | N/A (needs Python) | ~50KB static |
| Startup time | ~2s (Python import) | <0.1s |
| CPU usage | ~5-10% | ~2-4% |

---

## Estimated Effort

- **9 steps**, sequential
- Each step: 1–2 hours
- **Total: ~10–15 hours**
- Produces a native C animated wallpaper, ~50KB binary, no runtime deps beyond Cairo/X11
