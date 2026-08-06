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

#include "types.h"
#include "config.h"

/* ── Global WM state ─────────────────────────────────────── */
extern WMState *wm;

/* ── x11.c ───────────────────────────────────────────────── */
int  x11_connect(void);
void x11_subscribe_events(void);
void x11_cleanup(void);
void x11_grab_keys(void);
void x11_ungrab_keys(void);
int  x11_error_handler(Display *dpy, XErrorEvent *e);
int  x11_error_handler_startup(Display *dpy, XErrorEvent *e);

/* ── event loop (x11.c) ──────────────────────────────────── */
void event_loop(void);

/* ── client.c ────────────────────────────────────────────── */
Client    *client_add(Window win, Workspace *ws);
void       client_remove(Client *c);
Client    *client_find(Window win);
void       client_focus(Client *c);
void       client_unfocus(Client *c);
void       manage_window(Window win);
void       unmanage_window(Window win, int destroyed);
void       manage_existing_windows(void);

/* ── workspace.c ─────────────────────────────────────────── */
Workspace *workspace_get(int id);
void       workspace_switch(int id);
void       workspace_show(Workspace *ws);
void       workspace_hide(Workspace *ws);
void       client_move_to_workspace(Client *c, int id);

/* ── layout.c ────────────────────────────────────────────── */
void arrange_workspace(Workspace *ws);

/* ── input.c ─────────────────────────────────────────────── */
void keybind_process(XKeyEvent *e);

/* ── actions (input.c) — all share void(const char *) sig ── */
void action_spawn(const char *cmd);
void action_close_window(const char *arg);
void action_focus_next(const char *arg);
void action_focus_prev(const char *arg);
void action_toggle_floating(const char *arg);
void action_rotate_layout(const char *arg);
void action_gap_inc(const char *arg);
void action_gap_dec(const char *arg);
void action_quit(const char *arg);
void action_switch_workspace(const char *id);
void action_move_to_workspace(const char *id);

#endif /* SWORDWM_H */
