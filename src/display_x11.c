#include "display.h"

int display_init(WWDisplay *d) {
    d->dpy = XOpenDisplay(NULL);
    if (!d->dpy) {
        fprintf(stderr, "ww: cannot open display\n");
        return -1;
    }
    d->screen = DefaultScreen(d->dpy);
    d->root   = RootWindow(d->dpy, d->screen);
    d->sw     = DisplayWidth(d->dpy, d->screen);
    d->sh     = DisplayHeight(d->dpy, d->screen);
    d->surface = NULL;
    d->cr      = NULL;
    d->running = 1;
    return 0;
}

void display_create_window(WWDisplay *d) {
    XSetWindowAttributes wa;
    wa.override_redirect = True;
    wa.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask;

    d->win = XCreateWindow(d->dpy, d->root,
        0, 0, d->sw, d->sh, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWEventMask, &wa);

    XMapWindow(d->dpy, d->win);
}

void display_set_desktop_hints(WWDisplay *d) {
    Atom type   = XInternAtom(d->dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    Atom below  = XInternAtom(d->dpy, "_NET_WM_STATE_BELOW", False);
    Atom skip_t = XInternAtom(d->dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom skip_p = XInternAtom(d->dpy, "_NET_WM_STATE_SKIP_PAGER", False);
    Atom sticky = XInternAtom(d->dpy, "_NET_WM_DESKTOP", False);
    Atom state  = XInternAtom(d->dpy, "_NET_WM_STATE", False);
    Atom wtype  = XInternAtom(d->dpy, "_NET_WM_WINDOW_TYPE", False);
    long desktop = 0xFFFFFFFF;

    XChangeProperty(d->dpy, d->win, wtype, XA_ATOM, 32, PropModeReplace,
        (unsigned char *)&type, 1);

    Atom states[] = { below, skip_t, skip_p };
    XChangeProperty(d->dpy, d->win, state, XA_ATOM, 32, PropModeReplace,
        (unsigned char *)states, 3);

    XChangeProperty(d->dpy, d->win, sticky, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&desktop, 1);

    XSync(d->dpy, False);
}

void display_create_surface(WWDisplay *d) {
    if (d->surface) {
        cairo_destroy(d->cr);
        cairo_surface_destroy(d->surface);
    }
    d->surface = cairo_xlib_surface_create(d->dpy, d->win,
        DefaultVisual(d->dpy, d->screen), d->sw, d->sh);
    d->cr = cairo_create(d->surface);
}

void display_destroy(WWDisplay *d) {
    if (d->cr)      cairo_destroy(d->cr);
    if (d->surface) cairo_surface_destroy(d->surface);
    if (d->win)     XDestroyWindow(d->dpy, d->win);
    if (d->dpy)     XCloseDisplay(d->dpy);
}
