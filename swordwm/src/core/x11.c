/* =========================================================
 * src/core/x11.c — X11 connection, event loop, signal handling
 * ========================================================= */
#include "swordwm.h"
#include <X11/Xproto.h>

/* ── Signal flag ─────────────────────────────────────────── */
static volatile int g_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

/* ── X11 error handlers ──────────────────────────────────── */

/* Called during startup scan — silently ignore errors for windows
 * that disappear between XQueryTree and our attempts to manage them. */
int x11_error_handler_startup(Display *dpy, XErrorEvent *e) {
    (void)dpy;
    (void)e;
    return 0;
}

/* Normal runtime error handler — log and continue. */
int x11_error_handler(Display *dpy, XErrorEvent *e) {
    char buf[256];
    XGetErrorText(dpy, e->error_code, buf, sizeof(buf));
    fprintf(stderr, "swordwm: X error: %s (request %u, resource 0x%lx)\n",
            buf, e->request_code, e->resourceid);
    return 0;
}

/* ── Intern all EWMH / ICCCM atoms ──────────────────────── */
static void intern_atoms(void) {
#define INTERN(field, name) \
    wm->field = XInternAtom(wm->dpy, name, False)

    INTERN(wm_protocols,              "WM_PROTOCOLS");
    INTERN(wm_delete_window,          "WM_DELETE_WINDOW");
    INTERN(wm_state,                  "WM_STATE");
    INTERN(net_supported,             "_NET_SUPPORTED");
    INTERN(net_wm_name,               "_NET_WM_NAME");
    INTERN(net_active_window,         "_NET_ACTIVE_WINDOW");
    INTERN(net_client_list,           "_NET_CLIENT_LIST");
    INTERN(net_current_desktop,       "_NET_CURRENT_DESKTOP");
    INTERN(net_wm_desktop,            "_NET_WM_DESKTOP");
    INTERN(net_wm_state,              "_NET_WM_STATE");
    INTERN(net_wm_state_fullscreen,   "_NET_WM_STATE_FULLSCREEN");
    INTERN(net_wm_window_type,        "_NET_WM_WINDOW_TYPE");
    INTERN(net_wm_window_type_dialog, "_NET_WM_WINDOW_TYPE_DIALOG");
    INTERN(net_wm_window_type_splash, "_NET_WM_WINDOW_TYPE_SPLASH");
    INTERN(net_wm_window_type_utility,"_NET_WM_WINDOW_TYPE_UTILITY");
    INTERN(net_wm_window_type_desktop,"_NET_WM_WINDOW_TYPE_DESKTOP");
    INTERN(net_wm_window_type_dock,   "_NET_WM_WINDOW_TYPE_DOCK");
#undef INTERN
}

/* ── Advertise EWMH support ──────────────────────────────── */
static void set_ewmh_support(void) {
    Atom supported[] = {
        wm->net_supported,
        wm->net_wm_name,
        wm->net_active_window,
        wm->net_client_list,
        wm->net_current_desktop,
        wm->net_wm_desktop,
        wm->net_wm_state,
        wm->net_wm_state_fullscreen,
        wm->net_wm_window_type,
        wm->net_wm_window_type_dialog,
        wm->net_wm_window_type_splash,
        wm->net_wm_window_type_utility,
        wm->net_wm_window_type_desktop,
        wm->net_wm_window_type_dock,
    };
    int n = (int)(sizeof(supported) / sizeof(supported[0]));
    XChangeProperty(wm->dpy, wm->root, wm->net_supported,
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)supported, n);

    /* _NET_NUMBER_OF_DESKTOPS */
    Atom num_desktops = XInternAtom(wm->dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    long nd = NUM_WORKSPACES;
    XChangeProperty(wm->dpy, wm->root, num_desktops,
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&nd, 1);

    /* _NET_CURRENT_DESKTOP = 0 */
    long cur = 0;
    XChangeProperty(wm->dpy, wm->root, wm->net_current_desktop,
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&cur, 1);
}

/* ── Initialize workspaces ───────────────────────────────── */
static void init_workspaces(void) {
    wm->workspaces    = NULL;
    wm->num_workspaces = NUM_WORKSPACES;

    Workspace *prev = NULL;
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        Workspace *ws  = calloc(1, sizeof(Workspace));
        ws->id         = i;
        snprintf(ws->name, sizeof(ws->name), "%d", i + 1);
        ws->layout     = DEFAULT_LAYOUT;
        ws->gap        = GAP_INNER;
        ws->head       = NULL;
        ws->focused    = NULL;
        ws->next       = NULL;

        if (prev == NULL)
            wm->workspaces = ws;
        else
            prev->next = ws;
        prev = ws;
    }
    wm->current_ws = wm->workspaces; /* start on workspace 1 */
}

/* ── x11_connect ─────────────────────────────────────────── */
int x11_connect(void) {
    wm = calloc(1, sizeof(WMState));
    if (!wm) {
        perror("calloc");
        return -1;
    }

    wm->dpy = XOpenDisplay(NULL);
    if (!wm->dpy) {
        fprintf(stderr, "swordwm: cannot open X display '%s'\n",
                XDisplayName(NULL));
        free(wm);
        wm = NULL;
        return -1;
    }

    wm->screen  = DefaultScreen(wm->dpy);
    wm->root    = RootWindow(wm->dpy, wm->screen);
    wm->sw      = DisplayWidth(wm->dpy, wm->screen);
    wm->sh      = DisplayHeight(wm->dpy, wm->screen);
    wm->running = 1;

    fprintf(stderr, "swordwm: display %s, screen %dx%d\n",
            DisplayString(wm->dpy), wm->sw, wm->sh);

    /* Fail fast if another WM already owns SubstructureRedirect */
    XSetErrorHandler(x11_error_handler_startup);
    XSelectInput(wm->dpy, wm->root, SubstructureRedirectMask);
    XSync(wm->dpy, False);
    XSetErrorHandler(x11_error_handler);

    intern_atoms();
    set_ewmh_support();
    init_workspaces();

    x11_subscribe_events();
    x11_grab_keys();

    /* Set root window background */
    XSetWindowBackground(wm->dpy, wm->root,
        BlackPixel(wm->dpy, wm->screen));
    XClearWindow(wm->dpy, wm->root);

    /* Signal handlers for clean exit */
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP,  handle_signal);

    XSync(wm->dpy, False);
    return 0;
}

/* ── x11_subscribe_events ────────────────────────────────── */
void x11_subscribe_events(void) {
    XSelectInput(wm->dpy, wm->root,
        SubstructureRedirectMask |  /* intercept MapRequest, ConfigureRequest */
        SubstructureNotifyMask   |  /* UnmapNotify, DestroyNotify             */
        StructureNotifyMask      |  /* ConfigureNotify on root                */
        PropertyChangeMask       |  /* PropertyNotify                         */
        KeyPressMask             |  /* keyboard shortcuts                     */
        ButtonPressMask             /* mouse clicks on root                   */
    );
}

/* ── x11_grab_keys ───────────────────────────────────────── */
static const KeyBinding keybindings[] = { KEYBINDINGS };
static const int n_keybindings = (int)(sizeof(keybindings) / sizeof(keybindings[0]));

void x11_grab_keys(void) {
    XUngrabKey(wm->dpy, AnyKey, AnyModifier, wm->root);

    /* Modifiers to also grab: combinations with NumLock / CapsLock */
    unsigned int mods[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };

    for (int i = 0; i < n_keybindings; i++) {
        KeyCode kc = XKeysymToKeycode(wm->dpy, keybindings[i].keysym);
        if (!kc) continue;
        for (int m = 0; m < 4; m++) {
            XGrabKey(wm->dpy, kc,
                     keybindings[i].modmask | mods[m],
                     wm->root, True,
                     GrabModeAsync, GrabModeAsync);
        }
    }
}

void x11_ungrab_keys(void) {
    XUngrabKey(wm->dpy, AnyKey, AnyModifier, wm->root);
}

/* ── Event handlers (stubs + real implementations) ──────── */

static void handle_map_request(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;
    manage_window(ev->window);
}

static void handle_unmap_notify(XEvent *e) {
    XUnmapEvent *ev = &e->xunmap;
    /* Only care about send_event=True unmaps or client-initiated unmaps */
    if (ev->send_event || client_find(ev->window))
        unmanage_window(ev->window, 0);
}

static void handle_destroy_notify(XEvent *e) {
    XDestroyWindowEvent *ev = &e->xdestroywindow;
    unmanage_window(ev->window, 1);
}

static void handle_configure_request(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    Client *c = client_find(ev->window);

    if (c && !c->floating) {
        /* Managed tiled client: ignore position/size changes, re-arrange */
        arrange_workspace(wm->current_ws);
        return;
    }

    /* Unmanaged or floating window: honour the request */
    XWindowChanges wc;
    wc.x            = ev->x;
    wc.y            = ev->y;
    wc.width        = ev->width;
    wc.height       = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling      = ev->above;
    wc.stack_mode   = ev->detail;
    XConfigureWindow(wm->dpy, ev->window, (unsigned)ev->value_mask, &wc);
}

static void handle_property_notify(XEvent *e) {
    XPropertyEvent *ev = &e->xproperty;
    Client *c = client_find(ev->window);
    if (!c) return;

    if (ev->atom == XA_WM_NAME || ev->atom == wm->net_wm_name) {
        /* Update stored title */
        XFetchName(wm->dpy, c->win, &(char *){ NULL });
        char *name = NULL;
        if (XFetchName(wm->dpy, c->win, &name) && name) {
            strncpy(c->title, name, sizeof(c->title) - 1);
            c->title[sizeof(c->title) - 1] = '\0';
            XFree(name);
        }
    }
}

static void handle_client_message(XEvent *e) {
    XClientMessageEvent *ev = &e->xclient;
    Client *c = client_find(ev->window);
    if (!c) return;

    if (ev->message_type == wm->net_wm_state) {
        /* _NET_WM_STATE_FULLSCREEN toggle */
        Atom action = (Atom)ev->data.l[0]; /* 0=remove, 1=add, 2=toggle */
        Atom prop   = (Atom)ev->data.l[1];
        if (prop == wm->net_wm_state_fullscreen) {
            int want_fs = (action == 1) ||
                          (action == 2 && !c->fullscreen);
            c->fullscreen = want_fs;
            if (want_fs) {
                XMoveResizeWindow(wm->dpy, c->frame,
                                  0, 0, wm->sw, wm->sh);
                XMoveResizeWindow(wm->dpy, c->win,
                                  0, 0, wm->sw, wm->sh);
            } else {
                arrange_workspace(c->ws);
            }
        }
    }
}

static void handle_key_press(XEvent *e) {
    keybind_process(&e->xkey);
}

static void handle_button_press(XEvent *e) {
    XButtonEvent *ev = &e->xbutton;
    /* Click on root window — focus root */
    if (ev->window == wm->root) return;

    /* Click on a frame: focus that client */
    for (Workspace *ws = wm->workspaces; ws; ws = ws->next) {
        for (Client *c = ws->head; c; c = c->next) {
            if (c->frame == ev->window || c->win == ev->window) {
                client_focus(c);
                XAllowEvents(wm->dpy, ReplayPointer, CurrentTime);
                return;
            }
        }
    }
}

static void handle_enter_notify(XEvent *e) {
#if FOCUS_FOLLOWS_MOUSE
    XCrossingEvent *ev = &e->xcrossing;
    if (ev->mode != NotifyNormal || ev->detail == NotifyInferior) return;
    Client *c = client_find(ev->window);
    if (c) client_focus(c);
#else
    (void)e;
#endif
}

/* ── Dispatch table ──────────────────────────────────────── */
static void (*handlers[LASTEvent])(XEvent *) = {
    [MapRequest]      = handle_map_request,
    [UnmapNotify]     = handle_unmap_notify,
    [DestroyNotify]   = handle_destroy_notify,
    [ConfigureRequest]= handle_configure_request,
    [PropertyNotify]  = handle_property_notify,
    [ClientMessage]   = handle_client_message,
    [KeyPress]        = handle_key_press,
    [ButtonPress]     = handle_button_press,
    [EnterNotify]     = handle_enter_notify,
};

/* ── event_loop ──────────────────────────────────────────── */
void event_loop(void) {
    XEvent ev;
    while (wm->running && g_running) {
        XNextEvent(wm->dpy, &ev);
        if (handlers[ev.type])
            handlers[ev.type](&ev);
    }
}

/* ── x11_cleanup ─────────────────────────────────────────── */
void x11_cleanup(void) {
    if (!wm || !wm->dpy) return;

    x11_ungrab_keys();

    /* Delete EWMH properties we set on root */
    XDeleteProperty(wm->dpy, wm->root, wm->net_client_list);
    XDeleteProperty(wm->dpy, wm->root, wm->net_active_window);

    XSync(wm->dpy, False);
    XSetInputFocus(wm->dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    XCloseDisplay(wm->dpy);
    wm->dpy = NULL;

    /* Free workspaces */
    Workspace *ws = wm->workspaces;
    while (ws) {
        Workspace *next = ws->next;
        free(ws);
        ws = next;
    }
    wm->workspaces = NULL;
}
