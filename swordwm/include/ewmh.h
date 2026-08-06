#ifndef EWMH_H
#define EWMH_H

/* =========================================================
 * include/ewmh.h — EWMH / ICCCM property management
 * ========================================================= */

#include "types.h"

/* Called once after x11_connect() — sets up _NET_SUPPORTED,
 * _NET_SUPPORTING_WM_CHECK, _NET_NUMBER_OF_DESKTOPS, etc. */
void ewmh_init(void);

/* Update _NET_CLIENT_LIST and _NET_CLIENT_LIST_STACKING. */
void ewmh_update_client_list(void);

/* Update _NET_ACTIVE_WINDOW. */
void ewmh_update_active_window(Client *c);

/* Update _NET_CURRENT_DESKTOP. */
void ewmh_update_current_desktop(int id);

/* Set _NET_WM_DESKTOP on a client window. */
void ewmh_set_client_desktop(Client *c, int id);

/* Handle incoming _NET_WM_STATE ClientMessage for a client.
 * action: 0=remove, 1=add, 2=toggle */
void ewmh_handle_state(Client *c, long action, Atom prop1, Atom prop2);

/* Set WM_STATE property (ICCCM — NormalState / IconicState). */
void ewmh_set_wm_state(Client *c, long state);

/* Read _NET_WM_NAME (with WM_NAME fallback) into c->title. */
void ewmh_read_title(Client *c);

/* Read _NET_WM_WINDOW_TYPE — returns the type Atom or None. */
Atom ewmh_window_type(Window win);

/* Announce _NET_DESKTOP_NAMES for all workspaces. */
void ewmh_update_desktop_names(void);

/* Announce _NET_WORKAREA (usable screen area). */
void ewmh_update_workarea(void);

/* Set _NET_WM_PID on a client (reads client's own PID if set). */
void ewmh_set_pid(Client *c);

/* Set _NET_FRAME_EXTENTS on a client window.
 * Reports how many pixels our frame adds: left, right, top, bottom. */
void ewmh_set_frame_extents(Client *c);

/* Read _NET_WM_STRUT/_NET_WM_STRUT_PARTIAL from a dock/panel window
 * and update _NET_WORKAREA to exclude the reserved space. */
void ewmh_apply_strut(Window win);

#endif /* EWMH_H */
