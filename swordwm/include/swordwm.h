#ifndef SWORDWM_H
#define SWORDWM_H

/* =========================================================
 * swordwm.h — main header, included by every source file
 * ========================================================= */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>

#include "types.h"
#include "config.h"
#include "decorate.h"
#include "ewmh.h"
#include "physics.h"

/* ── Global WM state ─────────────────────────────────────── */
extern WMState *wm;

/* ── x11.c ───────────────────────────────────────────────── */
/** @brief Open the X display, intern atoms, init workspaces and input. */
int  x11_connect(void);
/** @brief Subscribe to root window events (SubstructureRedirect, KeyPress, etc.). */
void x11_subscribe_events(void);
/** @brief Release X resources, free workspaces, close display. */
void x11_cleanup(void);
/** @brief Grab all configured keybindings on the root window. */
void x11_grab_keys(void);
/** @brief Ungrab all keys from the root window (called before re-grab on reload). */
void x11_ungrab_keys(void);
/** @brief Runtime X error handler — logs the error and continues. */
int  x11_error_handler(Display *dpy, XErrorEvent *e);
/** @brief Startup X error handler — silently ignores errors during initial window scan. */
int  x11_error_handler_startup(Display *dpy, XErrorEvent *e);

/* ── event loop (x11.c) ──────────────────────────────────── */
/** @brief Dispatch one XEvent to the appropriate handler. */
void dispatch_event(XEvent *ev);

/* ── client.c ────────────────────────────────────────────── */
/** @brief Allocate a Client, create its frame window, reparent win into it, and map both. */
Client    *client_add(Window win, Workspace *ws);
/** @brief Unlink client from its workspace list, destroy its frame, and free it. */
void       client_remove(Client *c);
/** @brief Find the Client whose win or frame matches the given X window ID. */
Client    *client_find(Window win);
/** @brief Give keyboard focus to c, raise its frame, and update EWMH active window. */
void       client_focus(Client *c);
/** @brief Clear focused state and redraw title bar in unfocused colours. */
void       client_unfocus(Client *c);
/** @brief Take ownership of a newly mapped window — create client, arrange, focus. */
void       manage_window(Window win);
/** @brief Stop managing a window — remove client, reparent to root if not destroyed. */
void       unmanage_window(Window win, int destroyed);
/** @brief Adopt all pre-existing mapped windows found at startup. */
void       manage_existing_windows(void);

/* ── workspace.c ─────────────────────────────────────────── */
/** @brief Return the Workspace with the given 0-based id, or NULL if not found. */
Workspace *workspace_get(int id);
/** @brief Switch the active workspace: hide current, show and focus target. */
void       workspace_switch(int id);
/** @brief Map all client frames and windows on ws. */
void       workspace_show(Workspace *ws);
/** @brief Unmap all client frames and windows on ws. */
void       workspace_hide(Workspace *ws);
/** @brief Move client c to workspace id, rearranging both source and destination. */
void       client_move_to_workspace(Client *c, int id);

/* ── layout.c ────────────────────────────────────────────── */
/** @brief Run the active layout algorithm on ws and sync window positions. */
void arrange_workspace(Workspace *ws);

/* ── input.c ─────────────────────────────────────────────── */
/** @brief Match a KeyPress event against config and built-in bindings and fire the action. */
void keybind_process(XKeyEvent *e);

/* ── actions (input.c) — all share void(const char *) sig ── */
/** @brief Spawn cmd via /bin/sh -c; falls back to cfg.terminal if cmd is NULL. */
void action_spawn(const char *cmd);
/** @brief Send WM_DELETE_WINDOW to the focused client, or XKillClient as fallback. */
void action_close_focused(const char *arg);
/** @brief Move focus to the next client in the current workspace list. */
void action_focus_next(const char *arg);
/** @brief Move focus to the previous client in the current workspace list. */
void action_focus_prev(const char *arg);
/** @brief Toggle the focused client between floating and tiled. */
void action_toggle_floating(const char *arg);
/** @brief Cycle the current workspace's layout mode (tile → monocle → float → tile). */
void action_rotate_layout(const char *arg);
/** @brief Increase the current workspace's inner gap by 4px. */
void action_gap_inc(const char *arg);
/** @brief Decrease the current workspace's inner gap by 4px (floor 0). */
void action_gap_dec(const char *arg);
/** @brief Set wm->running = 0 to exit the event loop cleanly. */
void action_quit(const char *arg);
/** @brief Switch to workspace id (0-based string). */
void action_workspace_prev(const char *arg);

void action_workspace_next(const char *arg);

void action_switch_workspace(const char *id);
/** @brief Move the focused client to workspace id (0-based string). */
void action_move_to_workspace(const char *id);
/** @brief Reload config from disk, re-grab keys, and re-apply visual settings. */
void action_reload_config(const char *arg);
/** @brief Swap the focused client with the one before it in the stack. */
void action_move_stack_up(const char *arg);
/** @brief Swap the focused client with the one after it in the stack. */
void action_move_stack_down(const char *arg);
/** @brief Grow the master area by 5% (max MIN_MASTER_RATIO/MAX_MASTER_RATIO). */
void action_master_grow(const char *arg);
/** @brief Shrink the master area by 5% (min MIN_MASTER_RATIO/MAX_MASTER_RATIO). */
void action_master_shrink(const char *arg);

#endif /* SWORDWM_H */
