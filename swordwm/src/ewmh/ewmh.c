/* =========================================================
 * src/ewmh/ewmh.c — EWMH / ICCCM property management
 * ========================================================= */
#include "swordwm.h"
#include "ewmh.h"
#include "config_parser.h"

/* ── ewmh_init ───────────────────────────────────────────── */
void ewmh_init(void) {
    /* _NET_SUPPORTING_WM_CHECK — create a child window that
     * identifies us as the WM. Required by the EWMH spec. */
    Window check = XCreateSimpleWindow(wm->dpy, wm->root,
                                       0, 0, 1, 1, 0, 0, 0);
    Atom swc = XInternAtom(wm->dpy, "_NET_SUPPORTING_WM_CHECK", False);
    Atom wm_name_atom = XInternAtom(wm->dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(wm->dpy, "UTF8_STRING", False);

    /* Set on root and on the child window */
    XChangeProperty(wm->dpy, wm->root, swc, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&check, 1);
    XChangeProperty(wm->dpy, check, swc, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&check, 1);

    /* Identify WM by name */
    XChangeProperty(wm->dpy, check, wm_name_atom, utf8, 8,
                    PropModeReplace,
                    (unsigned char *)"swordwm", 7);

    /* _NET_NUMBER_OF_DESKTOPS */
    Atom nd_atom = XInternAtom(wm->dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    long nd = NUM_WORKSPACES;
    XChangeProperty(wm->dpy, wm->root, nd_atom, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&nd, 1);

    ewmh_update_desktop_names();
    ewmh_update_current_desktop(0);
    ewmh_update_workarea();

    /* Empty _NET_CLIENT_LIST to start */
    XChangeProperty(wm->dpy, wm->root, wm->net_client_list,
                    XA_WINDOW, 32, PropModeReplace, NULL, 0);

    /* Empty _NET_ACTIVE_WINDOW */
    Window none = None;
    XChangeProperty(wm->dpy, wm->root, wm->net_active_window,
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&none, 1);

    XSync(wm->dpy, False);
}

/* ── ewmh_update_client_list ─────────────────────────────── */
void ewmh_update_client_list(void) {
    Window wins[1024];
    int n = 0;
    for (Workspace *ws = wm->workspaces; ws && n < 1024; ws = ws->next)
        for (Client *c = ws->head; c && n < 1024; c = c->next)
            wins[n++] = c->win;

    XChangeProperty(wm->dpy, wm->root, wm->net_client_list,
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)wins, n);
}

/* ── ewmh_update_active_window ───────────────────────────── */
void ewmh_update_active_window(Client *c) {
    Window w = c ? c->win : None;
    XChangeProperty(wm->dpy, wm->root, wm->net_active_window,
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&w, 1);
}

/* ── ewmh_update_current_desktop ─────────────────────────── */
void ewmh_update_current_desktop(int id) {
    long cur = id;
    XChangeProperty(wm->dpy, wm->root, wm->net_current_desktop,
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&cur, 1);
}

/* ── ewmh_set_client_desktop ─────────────────────────────── */
void ewmh_set_client_desktop(Client *c, int id) {
    if (!c) return;
    long desktop = id;
    XChangeProperty(wm->dpy, c->win, wm->net_wm_desktop,
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&desktop, 1);
}

/* ── ewmh_handle_state ───────────────────────────────────── */
void ewmh_handle_state(Client *c, long action, Atom prop1, Atom prop2) {
    if (!c) return;

    /* Helper: apply action to a boolean flag */
    #define APPLY(flag, atom) \
        if (prop1 == (atom) || prop2 == (atom)) { \
            if      (action == 1) (flag) = 1; \
            else if (action == 0) (flag) = 0; \
            else                  (flag) = !(flag); \
        }

    APPLY(c->fullscreen, wm->net_wm_state_fullscreen)
    APPLY(c->floating,   wm->net_wm_window_type_dialog) /* not ideal but works */

    #undef APPLY

    /* Urgent / demands attention */
    Atom demands = XInternAtom(wm->dpy, "_NET_WM_STATE_DEMANDS_ATTENTION", False);
    if (prop1 == demands || prop2 == demands) {
        if      (action == 1) c->urgent = 1;
        else if (action == 0) c->urgent = 0;
        else                  c->urgent = !c->urgent;
    }

    /* Re-arrange if fullscreen changed */
    if (prop1 == wm->net_wm_state_fullscreen ||
        prop2 == wm->net_wm_state_fullscreen) {
        if (c->fullscreen) {
            XMoveResizeWindow(wm->dpy, c->frame,
                              0, 0, wm->sw, wm->sh);
            XMoveResizeWindow(wm->dpy, c->win,
                              0, 0, wm->sw, wm->sh);
            XRaiseWindow(wm->dpy, c->frame);
        } else {
            arrange_workspace(c->ws);
        }
    }
}

/* ── ewmh_set_wm_state (ICCCM WM_STATE) ─────────────────── */
void ewmh_set_wm_state(Client *c, long state) {
    if (!c) return;
    /* WM_STATE = { state, icon_window } — two longs */
    long data[2] = { state, None };
    XChangeProperty(wm->dpy, c->win, wm->wm_state,
                    wm->wm_state, 32, PropModeReplace,
                    (unsigned char *)data, 2);
}

/* ── ewmh_read_title ─────────────────────────────────────── */
void ewmh_read_title(Client *c) {
    if (!c) return;
    Atom utf8 = XInternAtom(wm->dpy, "UTF8_STRING", False);
    Atom actual; int fmt; unsigned long n, ba;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(wm->dpy, c->win, wm->net_wm_name,
                           0, 256, False, utf8,
                           &actual, &fmt, &n, &ba, &prop) == Success
        && prop) {
        strncpy(c->title, (char *)prop, sizeof(c->title) - 1);
        c->title[sizeof(c->title)-1] = '\0';
        XFree(prop);
        return;
    }
    char *name = NULL;
    if (XFetchName(wm->dpy, c->win, &name) && name) {
        strncpy(c->title, name, sizeof(c->title) - 1);
        c->title[sizeof(c->title)-1] = '\0';
        XFree(name);
    }
}

/* ── ewmh_window_type ────────────────────────────────────── */
Atom ewmh_window_type(Window win) {
    Atom actual; int fmt; unsigned long n, ba;
    unsigned char *prop = NULL;
    Atom result = None;

    if (XGetWindowProperty(wm->dpy, win, wm->net_wm_window_type,
                           0, 1, False, XA_ATOM,
                           &actual, &fmt, &n, &ba, &prop) == Success
        && prop) {
        result = *(Atom *)prop;
        XFree(prop);
    }
    return result;
}

/* ── ewmh_update_desktop_names ───────────────────────────── */
void ewmh_update_desktop_names(void) {
    /* Build a null-separated UTF-8 string: "1\02\03\0…9\0" */
    char buf[256];
    int  pos = 0;
    for (Workspace *ws = wm->workspaces; ws && pos < 240; ws = ws->next) {
        int len = (int)strlen(ws->name);
        memcpy(buf + pos, ws->name, (size_t)len);
        pos += len;
        buf[pos++] = '\0';
    }
    Atom names_atom = XInternAtom(wm->dpy, "_NET_DESKTOP_NAMES", False);
    Atom utf8       = XInternAtom(wm->dpy, "UTF8_STRING", False);
    XChangeProperty(wm->dpy, wm->root, names_atom, utf8, 8,
                    PropModeReplace, (unsigned char *)buf, pos);
}

/* ── ewmh_update_workarea ────────────────────────────────── */
void ewmh_update_workarea(void) {
    /* One entry per desktop: x, y, width, height.
     * Use only the configured outer gap here; actual dock/panel
     * reservations are applied later by ewmh_apply_strut. */
    long wa[4] = {
        cfg.gap_outer,
        cfg.gap_outer,
        wm->sw - cfg.gap_outer * 2,
        wm->sh - cfg.gap_outer * 2
    };
    long all[9 * 4];
    for (int i = 0; i < NUM_WORKSPACES; i++)
        for (int j = 0; j < 4; j++)
            all[i*4 + j] = wa[j];

    Atom wa_atom = XInternAtom(wm->dpy, "_NET_WORKAREA", False);
    XChangeProperty(wm->dpy, wm->root, wa_atom, XA_CARDINAL, 32,
                    PropModeReplace,
                    (unsigned char *)all, NUM_WORKSPACES * 4);
}

/* ── ewmh_set_pid ────────────────────────────────────────── */
void ewmh_set_pid(Client *c) {
    if (!c) return;
    Atom net_wm_pid = XInternAtom(wm->dpy, "_NET_WM_PID", False);
    /* Read the _NET_WM_PID the client set on itself, if any */
    Atom actual; int fmt; unsigned long n, ba;
    unsigned char *prop = NULL;
    if (XGetWindowProperty(wm->dpy, c->win, net_wm_pid,
                           0, 1, False, XA_CARDINAL,
                           &actual, &fmt, &n, &ba, &prop) == Success
        && prop) {
        /* Already set by the client — leave it alone */
        XFree(prop);
        return;
    }
    /* Client didn't set it — we can't know its PID, so skip */
}

/* ── ewmh_set_frame_extents ──────────────────────────────── */
void ewmh_set_frame_extents(Client *c) {
    if (!c) return;
    /*
     * _NET_FRAME_EXTENTS = { left, right, top, bottom }
     * Tells clients how much space our frame adds around them.
     */
    long extents[4] = {
        cfg.border_width,               /* left   */
        cfg.border_width,               /* right  */
        cfg.title_bar_height,           /* top    */
        cfg.border_width,               /* bottom */
    };
    Atom net_frame = XInternAtom(wm->dpy, "_NET_FRAME_EXTENTS", False);
    XChangeProperty(wm->dpy, c->win, net_frame,
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)extents, 4);
}

/* ── ewmh_apply_strut ────────────────────────────────────── */
/*
 * Read _NET_WM_STRUT_PARTIAL (or _NET_WM_STRUT) from a dock/panel
 * window and adjust the workarea accordingly.
 * Called when a dock window maps or changes its strut property.
 */
void ewmh_apply_strut(Window win) {
    Atom strut_p = XInternAtom(wm->dpy, "_NET_WM_STRUT_PARTIAL", False);
    Atom strut   = XInternAtom(wm->dpy, "_NET_WM_STRUT",         False);
    Atom actual; int fmt; unsigned long n, ba;
    unsigned char *prop = NULL;
    long s[12] = {0};

    /* Prefer _NET_WM_STRUT_PARTIAL (12 values) */
    if (XGetWindowProperty(wm->dpy, win, strut_p, 0, 12, False,
                           XA_CARDINAL, &actual, &fmt, &n, &ba,
                           &prop) == Success && prop && n >= 4) {
        for (unsigned long i = 0; i < n && i < 12; i++)
            s[i] = ((long *)prop)[i];
        XFree(prop);
    } else if (XGetWindowProperty(wm->dpy, win, strut, 0, 4, False,
                                  XA_CARDINAL, &actual, &fmt, &n, &ba,
                                  &prop) == Success && prop && n >= 4) {
        /* _NET_WM_STRUT: left, right, top, bottom */
        for (unsigned long i = 0; i < 4; i++)
            s[i] = ((long *)prop)[i];
        XFree(prop);
    } else {
        return;
    }

    /* s[0]=left s[1]=right s[2]=top s[3]=bottom */
    long left   = s[0];
    long right  = s[1];
    long top    = s[2];
    long bottom = s[3];

    /* Update workarea to exclude the reserved space */
    long wa[4] = {
        cfg.gap_outer + left,
        cfg.gap_outer + top,
        wm->sw - cfg.gap_outer * 2 - left - right,
        wm->sh - cfg.gap_outer * 2 - top  - bottom,
    };
    long all[9 * 4];
    for (int i = 0; i < NUM_WORKSPACES; i++)
        for (int j = 0; j < 4; j++)
            all[i*4 + j] = wa[j];

    Atom wa_atom = XInternAtom(wm->dpy, "_NET_WORKAREA", False);
    XChangeProperty(wm->dpy, wm->root, wa_atom, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)all,
                    NUM_WORKSPACES * 4);
}
