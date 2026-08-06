**SwordWM — X11 Window Manager in C**  
**Objective:** Build a hybrid tiling/stacking window manager for X11 in C, similar to i3/XFCE, as a learning project.  
**Language:** C (C11)  
   
 **Display Server:** X11 (Xlib)  
   
 **Build System:** Make  
   
 **Layout:** Hybrid (tiling + floating, switchable at runtime)  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANklEQVR4nO3OMQ2AABAAsSNBCkJfFEIwwIgHRiywEZJWQZeZ2ao9AAD+4lyruzq+ngAA8Nr1AOHsBegrsOrIAAAAAElFTkSuQmCC)  
**Architecture Overview**  
┌─────────────────────────────────────────────┐  
 │                  Main Loop                   │  
 │  ┌──────────┐  ┌──────────┐  ┌──────────┐  │  
 │  │  Events  │→ │ Dispatch │→ │ Handlers │  │  
 │  └──────────┘  └──────────┘  └──────────┘  │  
 │                                             │  
 │  ┌──────────────────────────────────────┐   │  
 │  │         Window Manager Core          │   │  
 │  │  ┌────────┐ ┌────────┐ ┌─────────┐  │   │  
 │  │  │ Window │ │ Layout │ │  Key    │  │   │  
 │  │  │ List   │ │ Engine │ │ Bindings│  │   │  
 │  │  └────────┘ └────────┘ └─────────┘  │   │  
 │  │  ┌────────┐ ┌────────┐ ┌─────────┐  │   │  
 │  │  │Focus   │ │Decorate│ │Desktops │  │   │  
 │  │  │Manager │ │ Engine │ │ Manager │  │   │  
 │  │  └────────┘ └────────┘ └─────────┘  │   │  
 │  └──────────────────────────────────────┘   │  
 │                                             │  
 │  ┌──────────────────────────────────────┐   │  
 │  │         X11 / Xlib Layer             │   │  
 │  │  Connection, Events, Atoms, Hints    │   │  
 │  └──────────────────────────────────────┘   │  
 └─────────────────────────────────────────────┘  
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OQQmAABRAsSd4NIGBzPXBmAawhhW8ibAl2DIze3UGAMBf3Gu1VcfXEwAAXrsehaQEN+8fLHEAAAAASUVORK5CYII=)  
**Phase 1: X11 Foundation (Steps 1–3)**  
**Step 1: Project Scaffold & Build System**  
**Depends on:** None  
   
 **Parallelizable:** No  
   
 **Estimated effort:** Small  
**Context:** Set up the project directory structure, Makefile, and core header files. This creates the skeleton everything else builds on.  
**Task list:**  
- Create directory structure: src/, src/core/, src/layout/, src/input/, src/decorate/, include/, config/  
- Write Makefile with targets: all, clean, install, debug  
- Create include/swordwm.h — main header with all type forward declarations  
- Create include/types.h — core types (Client, Workspace, Layout, KeyBinding)  
- Create config/config.h — default configuration constants (gaps, borders, colors, keybinds)  
- Create src/main.c — entry point skeleton with XOpenDisplay, error handling  
**Verification:**  
make clean && make    # Must compile with no errors  
 ./swordwm             # Should attempt connection (may fail if no X display)  
   
**Exit criteria:** make produces swordwm binary. Binary prints usage or connects to X.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANElEQVR4nO3OQQmAABRAsad4EjtY9fewnUms4E2ELcGWmTmrKwAA/uLeqrU6vp4AAPDa/gDzWAM6QQXRdAAAAABJRU5ErkJggg==)  
**Step 2: X11 Connection & Event Loop**  
**Depends on:** Step 1  
   
 **Parallelizable:** No  
   
 **Estimated effort:** Medium  
**Context:** The heart of any WM — connect to X, select events on root window, and run the event dispatch loop. This is the skeleton that all other features plug into.  
**Task list:**  
- Implement x11_connect() — XOpenDisplay, XDefaultScreen, get root window  
- Implement x11_subscribe_events() — XSelectInput on root: SubstructureRedirect, SubstructureNotify, KeyPress, PropertyChange  
- Implement event dispatch table — map event type to handler function pointer  
- Implement event_loop() — XNextEvent dispatch loop  
- Implement stub handlers for all events (MapRequest, UnmapNotify, ConfigureRequest, KeyPress, etc.)  
- Add running flag and SIGINT/SIGHUP signal handling for clean exit  
- Add x11_cleanup() — XUngrabKey, XDeleteProperty, XCloseDisplay  
**Verification:**  
make && ./swordwm    # Should grab root window events (test from TTY or Xephyr)  
 # In another terminal: xterm should NOT appear (WM is intercepting MapRequest)  
   
**Exit criteria:** WM runs, logs received events, and exits cleanly on Ctrl+C.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANklEQVR4nO3OMQ2AABAAsSPBCj7fFRYQwYwEZiywEZJWQZeZ2ao9AAD+4lyruzq+ngAA8Nr1AMTJBeJDClAyAAAAAElFTkSuQmCC)  
**Step 3: Window Tracking & Client List**  
**Depends on:** Step 2  
   
 **Parallelizable:** No  
   
 **Estimated effort:** Medium  
**Context:** The WM must track all managed windows. When a client maps, we create a Client struct and add it to our list. When it unmaps, we remove it. This is the data foundation for layout and focus.  
**Task list:**  
- Implement Client struct: Window win, Window frame, int x,y,w,h, int floating, int focused, Workspace *ws, Client *next, Client *prev  
- Implement Workspace struct: int id, Client *head, Client *focused, Layout layout, int gap, Workspace *next  
- Implement client list operations: client_add(), client_remove(), client_find(), client_focus()  
- Implement manage_window() — called on MapRequest: create Client, reparent into frame window, select events on client  
- Implement unmanage_window() — called on UnmapNotify/DestroyNotify: remove Client, reparent back to root, free  
- Handle existing windows on startup — XQueryTree to find all mapped windows and manage them  
- Frame window creation: XCreateSimpleWindow as parent with border  
**Verification:**  
make && ./swordwm &  
 # From another terminal:  
 xterm &    # Should get framed and tracked  
 xeyes &    # Second window should also be managed  
 # Check: xprop -root should show _NET_CLIENT_LIST  
   
**Exit criteria:** Windows get frames, are tracked in a linked list, and removed on close.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANklEQVR4nO3OQQmAABRAsScYxpg/i2XMYARvRrCCNxG2BFtmZquOAAD4i3Ot7mr/egIAwGvXA22YBcnkstSpAAAAAElFTkSuQmCC)  
**Phase 2: Layout Engine (Steps 4–6)**  
**Step 4: Basic Tiling Layout**  
**Depends on:** Step 3  
   
 **Parallelizable:** No  
   
 **Estimated effort:** Medium  
**Context:** The tiling engine arranges non-floating windows in non-overlapping rectangles. Start with a simple master/stack layout (like dwm's monocle and tile).  
**Task list:**  
- Implement layout_tile() — master/stack split: N/2 windows on left, rest stacked right  
- Implement layout_monocle() — all windows fullscreen (stacked, only focused visible)  
- Implement layout_floating() — no automatic arrangement, windows stay where placed  
- Implement arrange_workspace() — iterate clients, call current layout function  
- Handle gaps between windows (configurable pixel gap)  
- Handle screen edges (configurable outer gap)  
- Update frame geometry: XMoveResizeWindow on each frame  
- Call arrange_workspace() on client add/remove and workspace switch  
**Verification:**  
make && ./swordwm &  
 # Open 4 xterm windows — should tile automatically  
 # Check gaps between windows are consistent  
 # Close one — remaining should re-tile  
   
**Exit criteria:** Windows tile automatically, gaps work, close triggers re-tile.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANklEQVR4nO3OMQ2AABAAsSNhRAF6EPYDLhGADSywEZJWQZeZ2aszAAD+4l6rrTq+ngAA8Nr1AIWsBDYDm5cLAAAAAElFTkSuQmCC)  
**Step 5: Floating Mode & Toggle**  
**Depends on:** Step 4  
   
 **Parallelizable:** No  
   
 **Estimated effort:** Small  
**Context:** Hybrid WM means users can toggle windows between tiling and floating. Floating windows are positioned freely and sit above tiled windows.  
**Task list:**  
- Add floating flag to Client struct  
- Implement toggle_floating() — flip flag, re-arrange workspace  
- Implement client_set_floating() — XConfigureWindow stacking mode, bypass layout  
- Float specific window types by default (dialog, splash, utility — check WM_CLASS or _NET_WM_WINDOW_TYPE)  
- Keyboard shortcut to toggle focused window floating  
- Mouse move/resize for floating windows (ButtonPress on frame, MotionNotify)  
- Raise floating windows above tiled (configure stacking order)  
**Verification:**  
# Open xterm (tiles), press Mod+Shift+Space → should float  
 # Can drag floating window with mouse  
 # Open a dialog app — should auto-float  
   
**Exit criteria:** Windows toggle tiling/floating, floating windows can be dragged.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANElEQVR4nO3OQQmAABRAsad4EEtY9QcxnUms4E2ELcGWmTmrKwAA/uLeqrU6vp4AAPDa/gDzXgM37EF77AAAAABJRU5ErkJggg==)  
**Step 6: Workspaces (Virtual Desktops)**  
**Depends on:** Step 5  
   
 **Parallelizable:** No  
   
 **Estimated effort:** Medium  
**Context:** Multiple workspaces let users organize windows. Each workspace has its own layout and client list. Switching hides/shows all windows on that workspace.  
**Task list:**  
- Extend Workspace struct with id, name, layout, client list  
- Implement workspace_switch() — hide current workspace clients, show target workspace clients  
- Implement workspace_create() — lazy creation of workspaces on first use  
- Implement client_move_to_workspace() — move client between workspaces  
- Set _NET_WM_DESKTOP on each client window  
- Set _NET_CURRENT_DESKTOP on root  
- Keyboard shortcuts: Mod+1..9 to switch, Mod+Shift+1..9 to move client  
- Update EWMH properties on switch  
**Verification:**  
# Open xterm on workspace 1, xeyes on workspace 2  
 # Switch between workspaces — windows hide/show correctly  
 # Move xterm to workspace 2 — disappears from 1, appears on 2  
   
**Exit criteria:** 9 workspaces work, switch/move keyboard shortcuts functional.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANklEQVR4nO3OMQ2AABAAsSNBCkJfFSqwwIgHRiywEZJWQZeZ2ao9AAD+4lyruzq+ngAA8Nr1AOH8BeZxN/IIAAAAAElFTkSuQmCC)  
**Phase 3: Input & Focus (Steps 7–8)**  
**Step 7: Keyboard Shortcuts & Commands**  
**Depends on:** Step 6  
   
 **Parallelizable:** No  
   
 **Estimated effort:** Medium  
**Context:** i3-like WMs are keyboard-driven. Need a complete keybinding system with modifier+key → action mapping.  
**Task list:**  
- Implement KeyBinding struct: KeySym keysym, unsigned int modmask, void (*action)(void*), void *arg  
- Implement keybind_grab() — XGrabKey on root for each binding  
- Implement keybind_process() — match KeyPress against binding list  
- Implement all core actions:  
  - action_launch_terminal() — fork+exec ghostty  
  - action_close_window() — send WM_DELETE_WINDOW or XKillClient  
  - action_focus_next/prev() — cycle focus  
  - action_swap_next/prev() — swap positions  
  - action_rotate_layout() — cycle tile/monocle/floating  
  - action_gap_inc/dec() — adjust gaps  
  - action_reload_config() — SIGHUP handler  
  - action_quit() — clean exit  
- Read keybinds from config (or hardcode first, config file later)  
**Verification:**  
# Mod+Return → launches terminal  
 # Mod+q → closes focused window  
 # Mod+j/k → cycles focus  
 # Mod+Space → rotates layout  
   
**Exit criteria:** All core keyboard shortcuts work.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANklEQVR4nO3OUQmAABBAsSeYxZyXSzCJASxgACv4J8KWYMvMbNURAAB/ca7VXe1fTwAAeO16AKe+BdmJqrPdAAAAAElFTkSuQmCC)  
**Step 8: Input Focus Management**  
**Depends on:** Step 7  
   
 **Parallelizable:** No  
   
 **Estimated effort:** Medium  
**Context:** Proper focus is critical — the focused window receives keyboard input. Need to handle focus stealing prevention, input focus model (click-to-focus vs focus-follows-mouse).  
**Task list:**  
- Implement focus_client() — XSetInputFocus, raise, highlight border  
- Implement unfocus_client() — dim border, restore previous focus  
- Implement click-to-focus: ButtonPress on window → focus it  
- Implement focus-follows-mouse (optional, configurable)  
- Handle WM_TAKE_FOCUS protocol — send WM_PROTOCOLS client message  
- Handle FocusIn/FocusOut events — update internal state  
- Prevent focus stealing: track _NET_ACTIVE_WINDOW, respect urgency hints  
- Visual feedback: change border color on focus/unfocus (configurable colors)  
- Handle focus when last window closes — focus next or root  
**Verification:**  
# Click on different windows → border color changes  
 # Type in focused window → correct window receives input  
 # Close focused window → focus moves to next  
   
**Exit criteria:** Focus tracking is correct, visual feedback works, keyboard input goes to right window.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OQQmAABRAsSd4NIGBzPXBmAawhhW8ibAl2DIze3UGAMBf3Gu1VcfXEwAAXrsehaQEN+8fLHEAAAAASUVORK5CYII=)  
**Phase 4: Decorations & EWMH (Steps 9–10)**  
**Step 9: Window Decorations (Title Bar)**  
**Depends on:** Step 8  
   
 **Parallelizable:** No  
   
 **Estimated effort:** Medium  
**Context:** Visual decorations: title bar with window name, close/minimize buttons, border around window. Can be drawn with Xlib primitives or use a library.  
**Task list:**  
- Design frame layout: title bar (24px) + client area + border (2px)  
- Implement title bar drawing: XFillRectangle for background, XDrawString for title  
- Add close button (X on right side of title bar)  
- Add workspace indicator in title bar  
- Update title on PropertyNotify (WM_NAME, _NET_WM_NAME changes)  
- Handle frame mouse events: drag to move, resize from edges/corners  
- Double-click title bar to toggle floating  
- Colors: focused vs unfocused title bar (configurable)  
**Verification:**  
# Windows show title bar with name  
 # Drag title bar to move window  
 # Close button works  
 # Focus change updates title bar color  
   
**Exit criteria:** Decorated windows with functional title bars.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OQQ2AQBAAsSE5CbzRujLwhwQMYIEfIWkVdJuZozoDAOAvrlWtav96AgDAa/cDEXQEKquakOYAAAAASUVORK5CYII=)  
**Step 10: EWMH & ICCCM Compliance**  
**Depends on:** Step 9  
   
 **Parallelizable:** No  
   
 **Estimated effort:** Medium  
**Context:** Extended Window Manager Hints (EWMH) let panels, docks, and other apps interact with the WM. ICCCM is the basic X11 window management protocol.  
**Task list:**  
- Implement _NET_SUPPORTED — advertise which EWMH properties we support  
- Implement _NET_SUPPORTING_WM_CHECK — WM identification  
- Implement _NET_CLIENT_LIST — list of managed windows  
- Implement _NET_ACTIVE_WINDOW — currently focused window  
- Implement _NET_WORKINGAREA — usable area (screen minus gaps/decorations)  
- Implement _NET_WM_DESKTOP — workspace assignment per window  
- Implement _NET_CURRENT_DESKTOP — active workspace  
- Implement _NET_WM_NAME, _NET_WM_STATE — window metadata  
- Handle _NET_WM_STATE_FULLSCREEN — toggle fullscreen  
- Handle _NET_WM_STATE_DEMANDS_ATTENTION — urgent hints  
- ICCCM: WM_DELETE_WINDOW protocol, WM_STATE property, WM_HINTS  
- Test with xprop, xdotool, wmctrl, panel apps like polybar or tint2  
**Verification:**  
# xprop on root shows all EWMH properties  
 # wmctrl -l lists managed windows  
 # _NET_CLIENT_LIST updates when windows open/close  
 # Panel app (tint2/polybar) shows window list correctly  
   
**Exit criteria:** EWMH properties correct, external tools work with WM.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANElEQVR4nO3OUQmAABBAsSdYxKYXx1gmEBOIFfwTYUuwZWa2ag8AgL841uquzq8nAAC8dj05WgYLQTzjnAAAAABJRU5ErkJggg==)  
**Phase 5: Polish & Config (Steps 11–12)**  
**Step 11: Configuration File**  
**Depends on:** Step 10  
   
 **Parallelizable:** No  
   
 **Estimated effort:** Medium  
**Context:** Users need to configure the WM without recompiling. Parse a config file for keybinds, colors, gaps, autostart.  
**Task list:**  
- Design config file format (simple key=value or use a library like libconfig)  
- Implement config parser in src/config/config.c  
- Configurable items:  
  - Terminal emulator command  
  - Modifier key (Mod4=Super, Mod1=Alt)  
  - Keybindings (key → action)  
  - Colors (focused/unfocused border, title bar)  
  - Gaps (inner, outer)  
  - Border width  
  - Layout per workspace  
  - Autostart commands  
- Config file location: ~/.config/swordwm/config  
- Config reload: SIGHUP or Mod+Shift+r  
- Default config fallback if file missing  
**Verification:**  
# Edit ~/.config/swordwm/config → Mod+Shift+r → changes apply  
 # Change terminal command → Mod+Return launches new terminal  
 # Change colors → border colors update  
   
**Exit criteria:** Config file works, all settings apply without recompile.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANklEQVR4nO3OMQ2AABAAsSNBCkJfFEIwwIgHRiywEZJWQZeZ2ao9AAD+4lyruzq+ngAA8Nr1AOHsBegrsOrIAAAAAElFTkSuQmCC)  
**Step 12: Documentation & Packaging**  
**Depends on:** Step 11  
   
 **Parallelizable:** No  
   
 **Estimated effort:** Small  
**Context:** Write man page, README, and packaging for easy installation.  
**Task list:**  
- Write README.md with build instructions, features, default keybinds  
- Write man/swordwm.1 man page  
- Write docs/keybinds.md — complete keybinding reference  
- Write docs/architecture.md — code architecture overview  
- Add make install target (copies binary to /usr/local/bin, config to /etc/xdg)  
- Add desktop file for display managers  
- Test on clean system: build, install, launch from display manager  
**Verification:**  
make install    # Installs to /usr/local  
 man swordwm     # Man page readable  
 # Launch from display manager → works  
   
**Exit criteria:** Fully documented, installable WM.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OMQ2AABAAsSNhZscVjnidKEAGFtgISaugy8zs1RkAAH9xr9VWHV9PAAB47XoAor8EPg1yCpUAAAAASUVORK5CYII=)  
**File Structure**  
swordwm/  
 ├── Makefile  
 ├── BLUEPRINT.md  
 ├── README.md  
 ├── config/  
 │   └── swordwm.conf          # Default config  
 ├── include/  
 │   ├── swordwm.h             # Main header  
 │   ├── types.h               # Core types  
 │   ├── x11.h                 # X11 wrapper functions  
 │   ├── client.h              # Client management  
 │   ├── layout.h              # Layout engine  
 │   ├── input.h               # Keyboard/mouse input  
 │   ├── decorate.h            # Window decorations  
 │   ├── ewmh.h                # EWMH/ICCCM  
 │   └── config.h              # Configuration  
 ├── src/  
 │   ├── main.c                # Entry point  
 │   ├── core/  
 │   │   ├── x11.c             # X11 connection & events  
 │   │   ├── client.c          # Client list management  
 │   │   └── workspace.c       # Workspace management  
 │   ├── layout/  
 │   │   ├── tile.c            # Tiling layout  
 │   │   ├── monocle.c         # Monocle layout  
 │   │   └── floating.c        # Floating mode  
 │   ├── input/  
 │   │   ├── keyboard.c        # Keyboard bindings  
 │   │   └── mouse.c           # Mouse bindings  
 │   ├── decorate/  
 │   │   └── frame.c           # Window decorations  
 │   ├── ewmh/  
 │   │   └── ewmh.c            # EWMH properties  
 │   └── config/  
 │       └── config.c          # Config file parser  
 └── docs/  
     ├── keybinds.md  
     └── architecture.md  
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OMQ2AABAAsSNhwgJOUPcjIpnRgQU2QtIq6DIze3UGAMBf3Gu1VcfXEwAAXrseaJEEL8XMiYMAAAAASUVORK5CYII=)  
**Dependency Graph**  
Step 1 (Scaffold)  
   └── Step 2 (Event Loop)  
         └── Step 3 (Client List)  
               ├── Step 4 (Tiling)  
               │     └── Step 5 (Floating)  
               │           └── Step 6 (Workspaces)  
               │                 └── Step 7 (Keybindings)  
               │                       └── Step 8 (Focus)  
               │                             └── Step 9 (Decorations)  
               │                                   └── Step 10 (EWMH)  
               │                                         └── Step 11 (Config)  
               │                                               └── Step 12 (Docs)  
               └── (Steps 4-12 sequential)  
   
All steps are sequential — each builds on the previous.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANklEQVR4nO3OMQ2AABAAsSPBCj5fFgpQwYwEZiywEZJWQZeZ2ao9AAD+4lyruzq+ngAA8Nr1AMTRBeEgNK9YAAAAAElFTkSuQmCC)  
**Key Design Decisions**  
1. **Xlib over XCB** — Xlib is higher-level, easier to learn, more documentation. XCB is lower-level and more performant but harder for a first WM.  
2. **Linked list over array** — Simple, no realloc needed, easy to insert/remove. Fine for typical WM client counts (<100).  
3. **Single-threaded event loop** — X11 is inherently single-threaded. No need for threads.  
4. **Config file parsing** — Simple hand-written parser for key=value format. No external dependencies.  
5. **Frame windows** — Each client gets a parent frame window for decorations. This is the standard approach (dwm, openbox, etc).  
6. **EWMH from step 10** — Rather than scattering EWMH throughout, consolidate it. Easier to maintain and test.  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAAM0lEQVR4nO3KsQ0AIRAEsUW6Qij1KvnevhMSYmKQ7GiCGd09k3wBAOAVf+2o4wYAwE1qAdYuAy151mgcAAAAAElFTkSuQmCC)  
**Testing Strategy**  
- **Manual testing** with Xephyr (nested X server) for safe development  
- **xdotool** for simulating window operations  
- **xprop/xwininfo** for verifying window properties  
- **wmctrl** for testing EWMH compliance  
- **valgrind** for memory leak detection  
- **Multiple monitors** testing (Xinerama/XRandR integration if added)  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OMQ2AABAAsSNhYMMAKlD4OzrxgQU2QtIq6DIzR3UFAMBf3Gu1VefXEwAAXtsfSqADWz4G/HUAAAAASUVORK5CYII=)  
**Estimated Total Effort**  
- **12 steps**, all sequential  
- Each step: 1–3 hours of focused development  
- **Total: ~20–30 hours** for a complete, usable WM  
- Can be spread across multiple sessions  
