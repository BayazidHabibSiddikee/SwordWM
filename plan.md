# Plan: Animated Wallpaper in C

## Status: Step 1 COMPLETE

### Done

- [x] Step 1: X11 window + Cairo surface + gradient renderer
  - `src/main.c` — entry point, arg parsing, event loop, 60fps timing
  - `src/display_x11.c` — Xlib connection, fullscreen window, desktop hints
  - `src/renderers/gradient.c` — animated gradient with floating orbs
  - `src/renderer.c` — renderer dispatch
  - Binary: `swordwm-wallpaper` (27KB)
  - Verified: _NET_WM_WINDOW_TYPE_DESKTOP, BELOW, SKIP_TASKBAR/PAGER, sticky

### Remaining Steps

- [ ] Step 2: Particles renderer (120 floating dots, glow, velocity)
- [ ] Step 3: Wave renderer (4 layered sine waves)
- [ ] Step 4: Matrix renderer (katakana rain, needs Pango)
- [ ] Step 5: Graph renderer (audio visualizer, needs libpulse/pipewire)
- [ ] Step 6: CLI polish (--next, --stop, state file, PID file)
- [ ] Step 7: Config file (~/.config/swordwm-wallpaper/config)
- [ ] Step 8: Wayland stub + final Makefile

## How to Build

```bash
cd /home/sword/animated-wallpaper/SwordWM
make
./swordwm-wallpaper --mode gradient
```

## Dependencies

```bash
sudo pacman -S cairo libx11 pango pulseaudio
```

## Architecture

```
include/
├── ww.h              # Types, X11/Cairo includes
├── display.h         # Display abstraction
├── renderer.h        # Renderer interface + externs
└── renderers/
    └── gradient.h

src/
├── main.c            # Entry, event loop, 60fps
├── display_x11.c     # Xlib: window, hints, surface
├── renderer.c        # renderer_find() dispatch
└── renderers/
    └── gradient.c    # Gradient + orbs

Makefile              # gcc + pkg-config cairo x11
```

## Renderer Interface

```c
typedef struct {
    const char *name;
    void (*init)(int w, int h);
    void (*render)(cairo_t *cr, int w, int h, double t);
    void (*destroy)(void);
} WWRenderer;
```

To add a renderer:
1. Create `src/renderers/NAME.c`
2. Create `include/renderers/NAME.h`
3. Add `extern const WWRenderer renderer_NAME;` to `renderer.h`
4. Add to `SRCS` in Makefile
5. Add to `renderer_find()` list in `src/renderer.c`
