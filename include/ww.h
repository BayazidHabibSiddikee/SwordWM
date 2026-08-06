#ifndef WW_H
#define WW_H

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <math.h>

typedef struct {
    Display *dpy;
    Window root;
    Window win;
    int screen;
    int sw, sh;
    cairo_surface_t *surface;
    cairo_t *cr;
    int running;
} WWDisplay;

typedef struct {
    const char *name;
    void (*init)(int w, int h);
    void (*render)(cairo_t *cr, int w, int h, double t);
    void (*destroy)(void);
} WWRenderer;

#endif
