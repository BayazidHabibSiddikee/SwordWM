/* =========================================================
 * src/core/client.c — Client list, window management, frame creation
 * ========================================================= */
#include "swordwm.h"
#include "config_parser.h"

/* ── Helper: parse color string to X11 pixel ────────────── */
static unsigned long parse_color(const char *hex) {
    XColor c;
    Colormap cmap = DefaultColormap(wm->dpy, wm->screen);
    if (XParseColor(wm->dpy, cmap, hex, &c) &&
        XAllocColor(wm->dpy, cmap, &c))
        return c.pixel;
    return BlackPixel(wm->dpy, wm->screen);
}

/* ── Check if a window should auto-float ─────────────────── */
static int should_float(Window win) {
    /* Check _NET_WM_WINDOW_TYPE */
    Atom actual_type;
    int  actual_format;
    unsigned long n_items, bytes_after;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(wm->dpy, win, wm->net_wm_window_type,
                           0, 1, False, XA_ATOM,
                           &actual_type, &actual_format,
                           &n_items, &bytes_after, &prop) == Success) {
        if (prop && n_items > 0) {
            Atom type = *(Atom *)prop;
            XFree(prop);
            prop = NULL;
            if (type == wm->net_wm_window_type_dialog  ||
                type == wm->net_wm_window_type_splash   ||
                type == wm->net_wm_window_type_utility  ||
                type == wm->net_wm_window_type_desktop  ||
                type == wm->net_wm_window_type_dock)
                return 1;
        } else if (prop) {
            XFree(prop); /* Success but zero items — still must free */
            prop = NULL;
        }
    }

    /* Check WM_TRANSIENT_FOR — dialogs are transient */
    Window transient = None;
    if (XGetTransientForHint(wm->dpy, win, &transient) && transient != None)
        return 1;

    return 0;
}

/* ── Temporary error handler used while creating the frame ── */
static int g_create_error = 0;

static int client_add_error_handler(Display *dpy, XErrorEvent *e) {
    (void)dpy;
    (void)e;
    g_create_error = 1;
    return 0;
}

/* ── client_add ──────────────────────────────────────────── */
Client *client_add(Window win, Workspace *ws) {
    Client *c = calloc(1, sizeof(Client));
    if (!c) { perror("calloc"); return NULL; }

    c->win      = win;
    c->ws       = ws;
    c->floating = should_float(win);

    /* Get initial geometry */
    XWindowAttributes wa;
    XGetWindowAttributes(wm->dpy, win, &wa);
    c->x = wa.x;
    c->y = wa.y;
    c->w = wa.width;
    c->h = wa.height;

    /* Get window title (UTF-8 aware) */
    c->title[0] = '\0';  /* Initialize to empty */
    ewmh_read_title(c);  /* Handles _NET_WM_NAME (UTF-8) + fallback to WM_NAME */

    /* Read WM_NORMAL_HINTS for min/max size constraints and resize increments */
    XSizeHints hints;
    long supplied = 0;
    if (XGetWMNormalHints(wm->dpy, win, &hints, &supplied)) {
        c->min_w = (supplied & PMinSize) ? hints.min_width  : 0;
        c->min_h = (supplied & PMinSize) ? hints.min_height : 0;
        c->max_w = (supplied & PMaxSize) ? hints.max_width  : 0;
        c->max_h = (supplied & PMaxSize) ? hints.max_height : 0;
        c->inc_w = (supplied & PResizeInc) ? hints.width_inc  : 1;
        c->inc_h = (supplied & PResizeInc) ? hints.height_inc : 1;
    } else {
        /* Initialize to sensible defaults if no hints */
        c->min_w = c->min_h = 0;
        c->max_w = c->max_h = 0;
        c->inc_w = c->inc_h = 1;
    }

    /* Create frame window */
    XSetWindowAttributes fa;
    fa.border_pixel      = parse_color(COLOR_UNFOCUSED_BORDER);
    fa.background_pixel  = parse_color(COLOR_UNFOCUSED_TITLE_BG);
    fa.event_mask        = SubstructureRedirectMask |
                           SubstructureNotifyMask   |
                           ButtonPressMask          |
                           EnterWindowMask          |
                           ExposureMask;
    fa.override_redirect = False;

    /* Frame: x, y, w, h — title bar sits above client */
    int fx = c->x;
    int fy = c->y > TITLE_BAR_HEIGHT ? c->y - TITLE_BAR_HEIGHT : 0;
    int fw = c->w;
    int fh = c->h + TITLE_BAR_HEIGHT;

    /* Install a temporary error handler so X protocol errors from
     * XCreateWindow and XReparentWindow set g_create_error rather
     * than just being logged and silently ignored. */
    g_create_error = 0;
    XErrorHandler prev_handler = XSetErrorHandler(client_add_error_handler);

    c->frame = XCreateWindow(
        wm->dpy, wm->root,
        fx, fy, fw, fh,
        BORDER_WIDTH,
        CopyFromParent, InputOutput, CopyFromParent,
        CWBorderPixel | CWBackPixel | CWEventMask | CWOverrideRedirect,
        &fa
    );

    /* Flush so any async error from XCreateWindow arrives before we check.
     * XCreateWindow always returns a syntactically valid ID (allocated
     * client-side before the request is sent), so the return value itself
     * cannot signal failure — g_create_error is the only reliable signal. */
    XSync(wm->dpy, False);

    if (g_create_error) {
        XSetErrorHandler(prev_handler);
        fprintf(stderr,
                "swordwm: client_add: XCreateWindow failed for 0x%lx — "
                "dropping window\n", win);
        free(c);
        return NULL;
    }

    /* Reparent the client window into the frame */
    g_create_error = 0;
    XReparentWindow(wm->dpy, win, c->frame, 0, TITLE_BAR_HEIGHT);

    /* Flush again to catch BadMatch/BadWindow from XReparentWindow. */
    XSync(wm->dpy, False);

    XSetErrorHandler(prev_handler);

    if (g_create_error) {
        fprintf(stderr,
                "swordwm: client_add: XReparentWindow failed for 0x%lx — "
                "dropping window\n", win);
        XDestroyWindow(wm->dpy, c->frame);
        free(c);
        return NULL;
    }

    /* Subscribe to client events */
    XSelectInput(wm->dpy, win,
        PropertyChangeMask | EnterWindowMask |
        FocusChangeMask    | StructureNotifyMask);

    /* Map both frame and client — only if the target workspace is active.
     * During manage_existing_windows, windows may be assigned to non-current
     * workspaces; mapping them would make them appear on the wrong desktop. */
    if (ws == wm->current_ws) {
        XMapWindow(wm->dpy, c->frame);
        XMapWindow(wm->dpy, win);
    }

    /* Prepend to workspace client list */
    c->next = ws->head;
    c->prev = NULL;
    if (ws->head) ws->head->prev = c;
    ws->head = c;

    /* Set ICCCM WM_STATE = NormalState */
    ewmh_set_wm_state(c, 1 /* NormalState */);

    /* Set _NET_WM_DESKTOP */
    ewmh_set_client_desktop(c, ws->id);

    /* Tell clients how much space our frame adds */
    ewmh_set_frame_extents(c);

    /* Add to global client tracking for fast lookups */
    if (wm->num_clients < 512) {
        wm->all_clients[wm->num_clients++] = c;
    }

    ewmh_update_client_list();

    /* Draw the initial title bar */
    ewmh_read_title(c);
    decorate_draw(c);

    return c;
}

/* ── client_remove ───────────────────────────────────────── */
void client_remove(Client *c) {
    if (!c) return;

    Workspace *ws = c->ws;

    /* Unlink from list */
    if (c->prev) c->prev->next = c->next;
    else         ws->head      = c->next;
    if (c->next) c->next->prev = c->prev;

    if (ws->focused == c) {
        ws->focused = c->next ? c->next : c->prev;
    }
    if (wm->focused == c) {
        wm->focused = ws->focused;
    }

    /* Destroy frame */
    if (c->frame) {
        XDestroyWindow(wm->dpy, c->frame);
        c->frame = None;
    }

    /* Remove from global client tracking array */
    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->all_clients[i] == c) {
            /* Shift remaining clients down to fill the gap */
            for (int j = i; j < wm->num_clients - 1; j++) {
                wm->all_clients[j] = wm->all_clients[j + 1];
            }
            wm->num_clients--;
            break;
        }
    }

    free(c);
    ewmh_update_client_list();
}

/* ── client_find ─────────────────────────────────────────── */
Client *client_find(Window win) {
    /* Use global client array for O(n) lookup instead of O(n²) nested loops */
    for (int i = 0; i < wm->num_clients; i++) {
        Client *c = wm->all_clients[i];
        if (c->win == win || c->frame == win) {
            return c;
        }
    }
    return NULL;
}

/* ── client_focus ────────────────────────────────────────── */
void client_focus(Client *c) {
    /* Unfocus the previously focused client */
    if (wm->focused && wm->focused != c)
        client_unfocus(wm->focused);

    if (!c) {
        XSetInputFocus(wm->dpy, wm->root,
                       RevertToPointerRoot, CurrentTime);
        ewmh_update_active_window(NULL);
        wm->focused = NULL;
        return;
    }

    c->focused     = 1;
    c->urgent      = 0;
    c->ws->focused = c;
    wm->focused    = c;

    /* Raise frame */
    XRaiseWindow(wm->dpy, c->frame);

    /* Redraw title bar with focused colours */
    decorate_draw(c);

    /* Give keyboard focus */
    XSetInputFocus(wm->dpy, c->win,
                   RevertToPointerRoot, CurrentTime);

    /* Update EWMH */
    ewmh_update_active_window(c);
}

/* ── client_unfocus ──────────────────────────────────────── */
void client_unfocus(Client *c) {
    if (!c) return;
    c->focused = 0;
    decorate_draw(c);
}

/* ── manage_window ───────────────────────────────────────── */
void manage_window(Window win) {
    /* Don't manage if we already track it */
    if (client_find(win)) return;

    XWindowAttributes wa;
    if (!XGetWindowAttributes(wm->dpy, win, &wa)) return;

    /* Skip override-redirect windows (panels, docks, wallpaper) */
    if (wa.override_redirect) {
        /* Even though we don't manage it, read its strut so the
         * workarea respects the panel/dock reserved space. */
        ewmh_apply_strut(win);
        return;
    }

    /* Note: map_state is IsUnmapped when called from handle_map_request
     * (the WM maps the window itself via client_add). When called from
     * manage_existing_windows, the caller already filters for IsViewable. */

    Client *c = client_add(win, wm->current_ws);
    if (!c) return;

    /* Arrange and focus the new window */
    arrange_workspace(wm->current_ws);
    client_focus(c);

    fprintf(stderr, "swordwm: managed window 0x%lx \"%s\" (ws %d, %s)\n",
            win, c->title, c->ws->id + 1,
            c->floating ? "floating" : "tiled");
}

/* ── unmanage_window ─────────────────────────────────────── */
void unmanage_window(Window win, int destroyed) {
    Client *c = client_find(win);
    if (!c) return;

    Workspace *ws = c->ws;

    if (!destroyed) {
        /* Reparent client back to root before destroying frame.
         * The client sits at (0, TITLE_BAR_HEIGHT) relative to the frame,
         * so its absolute position is (c->x, c->y + cfg.title_bar_height).
         * Use startup error handler to suppress BadMatch/BadWindow
         * if the window was destroyed between UnmapNotify and here. */
        XSetErrorHandler(x11_error_handler_startup);
        XReparentWindow(wm->dpy, c->win, wm->root,
                        c->x, c->y + cfg.title_bar_height);
        XSync(wm->dpy, False);
        XSetErrorHandler(x11_error_handler);
    }

    fprintf(stderr, "swordwm: unmanaged window 0x%lx \"%s\"\n",
            win, c->title);

    client_remove(c);
    arrange_workspace(ws);

    /* Focus next available client */
    if (ws->focused)
        client_focus(ws->focused);
    else
        client_focus(NULL);
}

/* ── manage_existing_windows ─────────────────────────────── */
void manage_existing_windows(void) {
    Window root_return, parent_return;
    Window *children = NULL;
    unsigned int n = 0;

    /* Temporarily use the startup error handler */
    XSetErrorHandler(x11_error_handler_startup);

    if (XQueryTree(wm->dpy, wm->root,
                   &root_return, &parent_return,
                   &children, &n) && children) {
        for (unsigned int i = 0; i < n; i++) {
            XWindowAttributes wa;
            if (!XGetWindowAttributes(wm->dpy, children[i], &wa)) continue;
            if (wa.override_redirect) continue;
            if (wa.map_state == IsViewable)
                manage_window(children[i]);
        }
        XFree(children);
    }

    XSetErrorHandler(x11_error_handler);
    XSync(wm->dpy, False);
}
