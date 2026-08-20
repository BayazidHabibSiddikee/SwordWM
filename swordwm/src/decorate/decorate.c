/* =========================================================
 * src/decorate/decorate.c
 *
 * Pure-Xlib title bar: background fill, window name, close
 * button, drag-to-move, edge/corner resize.
 *
 * Layout (frame window):
 *
 *  ┌──────────────────────────────────────────[✕]─┐
 *  │  window title                             [X] │  <- TITLE_BAR_HEIGHT px
 *  ├────────────────────────────────────────────────┤
 *  │                client area                     │
 *  └────────────────────────────────────────────────┘
 * ========================================================= */

#include "swordwm.h"
#include "config_parser.h"
#include "decorate.h"
#include <X11/Xft/Xft.h>
#include <string.h>

/* ── Shared drawing resources ────────────────────────────── */
static GC        g_gc     = None;
static XftFont  *g_font   = NULL;
static Colormap  g_cmap;
static Visual   *g_visual;

/* ── Drag/resize state ───────────────────────────────────── */
typedef enum {
    DRAG_NONE = 0,
    DRAG_MOVE,
    DRAG_RESIZE_TL, DRAG_RESIZE_TR,
    DRAG_RESIZE_BL, DRAG_RESIZE_BR,
    DRAG_RESIZE_T,  DRAG_RESIZE_B,
    DRAG_RESIZE_L,  DRAG_RESIZE_R,
} DragMode;

static struct {
    DragMode mode;
    Client  *client;
    int      start_x, start_y;
    int      orig_fx, orig_fy;
    int      orig_fw, orig_fh;
    /* velocity tracking for physics_anim_drop_bounce */
    int      prev_x, prev_y;
    double   vel_x,  vel_y;
} g_drag;

/* ── Color helpers ───────────────────────────────────────── */
static unsigned long parse_hex(const char *hex) {
    XColor c;
    if (XParseColor(wm->dpy, g_cmap, hex, &c) &&
        XAllocColor(wm->dpy, g_cmap, &c))
        return c.pixel;
    return BlackPixel(wm->dpy, wm->screen);
}

static int make_xft(const char *hex, XftColor *out) {
    if (!XftColorAllocName(wm->dpy, g_visual, g_cmap, hex, out)) {
        fprintf(stderr, "swordwm: decorate: XftColorAllocName failed for '%s'\n", hex);
        return 0;
    }
    return 1;
}

/* ── Close button geometry ───────────────────────────────── */
#define BTN_W   18
#define BTN_H   18
#define BTN_PAD  3

static int btn_x(Client *c) { return c->w - BTN_W - BTN_PAD; }
static int btn_y(void)      { return (cfg.title_bar_height - BTN_H) / 2; }

/* ── decorate_init ───────────────────────────────────────── */
void decorate_init(void) {
    g_cmap   = DefaultColormap(wm->dpy, wm->screen);
    g_visual = DefaultVisual(wm->dpy, wm->screen);
    g_gc     = XCreateGC(wm->dpy, wm->root, 0, NULL);

    g_font = XftFontOpenName(wm->dpy, wm->screen, cfg.font);
    if (!g_font) {
        fprintf(stderr, "swordwm: decorate: font '%s' not found, "
                        "falling back to monospace\n", cfg.font);
        g_font = XftFontOpenName(wm->dpy, wm->screen, "monospace:size=9");
    }
    if (!g_font) {
        fprintf(stderr, "swordwm: decorate: monospace not found, "
                        "falling back to fixed\n");
        g_font = XftFontOpenName(wm->dpy, wm->screen, "fixed");
    }
    if (!g_font)
        fprintf(stderr, "swordwm: decorate: no usable font found — "
                        "title bar text will be skipped\n");

    memset(&g_drag, 0, sizeof(g_drag));
}

/* ── decorate_cleanup ────────────────────────────────────── */
void decorate_cleanup(void) {
    if (g_font) { XftFontClose(wm->dpy, g_font); g_font = NULL; }
    if (g_gc)   { XFreeGC(wm->dpy, g_gc);        g_gc   = None; }
}

/* ── decorate_draw ───────────────────────────────────────── */
void decorate_draw(Client *c) {
    if (!c || !c->frame) return;

    /* Use runtime cfg colors — they can be overridden from the config file */
    const char *bg_hex  = c->minimized   ? "#0d0e14"   /* darkened when minimized */
                        : c->focused    ? cfg.color_focused_title_bg
                                        : cfg.color_unfocused_title_bg;
    const char *fg_hex  = c->minimized   ? "#3e4451"   /* dim text when minimized */
                        : c->focused    ? cfg.color_focused_title_fg
                                        : cfg.color_unfocused_title_fg;
    const char *bdr_hex = c->urgent  ? cfg.color_urgent_border
                        : c->focused ? cfg.color_focused_border
                                     : cfg.color_unfocused_border;

    /* Background */
    XSetForeground(wm->dpy, g_gc, parse_hex(bg_hex));
    XFillRectangle(wm->dpy, c->frame, g_gc,
                   0, 0, (unsigned)c->w, (unsigned)cfg.title_bar_height);

    /* Separator line */
    XSetForeground(wm->dpy, g_gc, parse_hex(bdr_hex));
    XDrawLine(wm->dpy, c->frame, g_gc,
              0, cfg.title_bar_height - 1, c->w, cfg.title_bar_height - 1);

    /* Frame border (sets XSetWindowBorder colour via GC draw) */
    XSetWindowBorder(wm->dpy, c->frame, parse_hex(bdr_hex));

    /* Window title via Xft */
    if (g_font && c->title[0]) {
        XftDraw *xd = XftDrawCreate(wm->dpy, c->frame, g_visual, g_cmap);
        if (xd) {
            XftColor fg;
            if (make_xft(fg_hex, &fg)) {
                /* Truncate title so it doesn't overlap close button.
                 * Strategy: measure the full title once, then binary-search
                 * over byte-length to find the longest prefix that fits.
                 * Append "..." suffix. O(log n) Xft measurements instead
                 * of the previous O(n²) character-at-a-time loop. */
                char title[260];
                strncpy(title, c->title, sizeof(title) - 1);
                title[sizeof(title) - 1] = '\0';
                int max_w = btn_x(c) - 12;

                XGlyphInfo ext;
                int full_len = (int)strlen(title);
                XftTextExtentsUtf8(wm->dpy, g_font,
                                   (const FcChar8 *)title, full_len, &ext);
                if (ext.width > max_w && full_len > 4) {
                    /* Binary-search the longest byte-length that fits. */
                    const char *ellipsis = "...";
                    const int   elen     = 3;
                    int lo = 0, hi = full_len - 1;
                    XGlyphInfo eext;
                    XftTextExtentsUtf8(wm->dpy, g_font,
                                       (const FcChar8 *)ellipsis, elen, &eext);
                    int budget = max_w - eext.width;
                    while (lo < hi) {
                        int mid = (lo + hi + 1) / 2;
                        XftTextExtentsUtf8(wm->dpy, g_font,
                                           (const FcChar8 *)title, mid, &ext);
                        if (ext.width <= budget)
                            lo = mid;
                        else
                            hi = mid - 1;
                    }
                    /* lo is the safe byte count; append ellipsis */
                    if (lo > full_len - 1) lo = full_len - 1;
                    title[lo] = '\0';
                    strncat(title, ellipsis, sizeof(title) - (size_t)lo - 1);
                }

                int ty = (cfg.title_bar_height + g_font->ascent - g_font->descent) / 2;
                XftDrawStringUtf8(xd, &fg, g_font,
                                  8, ty,
                                  (const FcChar8 *)title,
                                  (int)strlen(title));

                XftColorFree(wm->dpy, g_visual, g_cmap, &fg);
            }
            XftDrawDestroy(xd);
        }
    }

    /* Close button background */
    int bx = btn_x(c), by = btn_y();
    XSetForeground(wm->dpy, g_gc,
                   parse_hex(c->focused ? "#2a2d3e" : "#1a1b26"));
    XFillRectangle(wm->dpy, c->frame, g_gc,
                   bx, by, BTN_W, BTN_H);

    /* Close button border */
    XSetForeground(wm->dpy, g_gc, parse_hex(bdr_hex));
    XDrawRectangle(wm->dpy, c->frame, g_gc,
                   bx, by, BTN_W - 1, BTN_H - 1);

    /* ✕ cross */
    XSetForeground(wm->dpy, g_gc, parse_hex("#f7768e"));
    XSetLineAttributes(wm->dpy, g_gc, 1, LineSolid, CapRound, JoinRound);
    int p = 4;
    XDrawLine(wm->dpy, c->frame, g_gc,
              bx+p, by+p, bx+BTN_W-p, by+BTN_H-p);
    XDrawLine(wm->dpy, c->frame, g_gc,
              bx+BTN_W-p, by+p, bx+p, by+BTN_H-p);
}

/* ── decorate_update_title ───────────────────────────────── */
void decorate_update_title(Client *c) {
    if (!c) return;
    Atom net_name = XInternAtom(wm->dpy, "_NET_WM_NAME",  False);
    Atom utf8     = XInternAtom(wm->dpy, "UTF8_STRING",   False);
    Atom actual; int fmt; unsigned long n, ba;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(wm->dpy, c->win, net_name, 0, 256,
                           False, utf8, &actual, &fmt, &n, &ba,
                           &prop) == Success && prop) {
        strncpy(c->title, (char *)prop, sizeof(c->title) - 1);
        c->title[sizeof(c->title)-1] = '\0';
        XFree(prop);
    } else {
        char *name = NULL;
        if (XFetchName(wm->dpy, c->win, &name) && name) {
            strncpy(c->title, name, sizeof(c->title) - 1);
            c->title[sizeof(c->title)-1] = '\0';
            XFree(name);
        }
    }
    decorate_draw(c);
}

/* ── Hit-test ────────────────────────────────────────────── */
#define EDGE 6

static int in_btn(Client *c, int x, int y) {
    return x >= btn_x(c) && x <= btn_x(c)+BTN_W &&
           y >= btn_y()  && y <= btn_y()+BTN_H;
}

static DragMode hit_test(Client *c, int x, int y) {
    int w = c->w, h = c->h;
    if (y < cfg.title_bar_height && !in_btn(c, x, y)) return DRAG_MOVE;
    if (x < EDGE   && y < EDGE)    return DRAG_RESIZE_TL;
    if (x > w-EDGE && y < EDGE)    return DRAG_RESIZE_TR;
    if (x < EDGE   && y > h-EDGE)  return DRAG_RESIZE_BL;
    if (x > w-EDGE && y > h-EDGE)  return DRAG_RESIZE_BR;
    if (y < EDGE)                  return DRAG_RESIZE_T;
    if (y > h-EDGE)                return DRAG_RESIZE_B;
    if (x < EDGE)                  return DRAG_RESIZE_L;
    if (x > w-EDGE)                return DRAG_RESIZE_R;
    return DRAG_NONE;
}

/* ── start_drag helper ───────────────────────────────────── */
static void start_drag(DragMode mode, Client *c, XButtonEvent *e) {
    g_drag.mode    = mode;
    g_drag.client  = c;
    g_drag.start_x = e->x_root;
    g_drag.start_y = e->y_root;
    g_drag.orig_fx = c->x;
    g_drag.orig_fy = c->y;
    g_drag.orig_fw = c->w;
    g_drag.orig_fh = c->h;
    XGrabPointer(wm->dpy, wm->root, False,
                 PointerMotionMask | ButtonReleaseMask,
                 GrabModeAsync, GrabModeAsync,
                 None, None, CurrentTime);
}

/* ── decorate_button_press ───────────────────────────────── */
int decorate_button_press(Client *c, XButtonEvent *e) {
    if (!c || e->window != c->frame) return 0;

    int lx = e->x, ly = e->y;

    /* Close button */
    if (e->button == Button1 && in_btn(c, lx, ly)) {
        int n; Atom *proto = NULL;
        Atom del  = XInternAtom(wm->dpy, "WM_DELETE_WINDOW", False);
        Atom prot = XInternAtom(wm->dpy, "WM_PROTOCOLS",     False);
        if (XGetWMProtocols(wm->dpy, c->win, &proto, &n)) {
            for (int i = 0; i < n; i++) {
                if (proto[i] == del) {
                    XEvent ev = {0};
                    ev.type                 = ClientMessage;
                    ev.xclient.window       = c->win;
                    ev.xclient.message_type = prot;
                    ev.xclient.format       = 32;
                    ev.xclient.data.l[0]    = (long)del;
                    ev.xclient.data.l[1]    = CurrentTime;
                    XSendEvent(wm->dpy, c->win, False, NoEventMask, &ev);
                    XFree(proto);
                    return 1;
                }
            }
            XFree(proto);
        }
        XKillClient(wm->dpy, c->win);
        return 1;
    }

    /* Double-click title bar → toggle floating */
    if (e->button == Button1 && ly < TITLE_BAR_HEIGHT && !in_btn(c, lx, ly)) {
        static Time last = 0;
        if (e->time - last < 300) {
            action_toggle_floating(NULL);
            last = 0;
            return 1;
        }
        last = e->time;
    }

    /* Mod+Button1 anywhere = move (check BEFORE plain drag so it isn't shadowed) */
    if (e->button == Button1 && (e->state & MOD_KEY)) {
        if (!c->floating) action_toggle_floating(NULL);
        start_drag(DRAG_MOVE, c, e);
        return 1;
    }

    /* Mod+Button3 anywhere = resize bottom-right */
    if (e->button == Button3 && (e->state & MOD_KEY)) {
        if (!c->floating) action_toggle_floating(NULL);
        start_drag(DRAG_RESIZE_BR, c, e);
        return 1;
    }

    /* Button1 drag on title bar / edges (no Mod key) */
    if (e->button == Button1) {
        DragMode m = hit_test(c, lx, ly);
        if (m != DRAG_NONE) {
            if (!c->floating && m == DRAG_MOVE)
                action_toggle_floating(NULL); /* pull out of tiling */
            start_drag(m, c, e);
            return 1;
        }
    }

    return 0;
}

/* ── decorate_motion ─────────────────────────────────────── */
void decorate_motion(Client *c, XMotionEvent *e) {
    (void)c;
    if (g_drag.mode == DRAG_NONE || !g_drag.client) return;

    Client *dc = g_drag.client;
    int dx = e->x_root - g_drag.start_x;
    int dy = e->y_root - g_drag.start_y;
    int nx = g_drag.orig_fx, ny = g_drag.orig_fy;
    int nw = g_drag.orig_fw, nh = g_drag.orig_fh;

    switch (g_drag.mode) {
        case DRAG_MOVE:       nx+=dx; ny+=dy; break;
        case DRAG_RESIZE_BR:  nw+=dx; nh+=dy; break;
        case DRAG_RESIZE_TL:  nx+=dx; ny+=dy; nw-=dx; nh-=dy; break;
        case DRAG_RESIZE_TR:  ny+=dy; nw+=dx; nh-=dy; break;
        case DRAG_RESIZE_BL:  nx+=dx; nw-=dx; nh+=dy; break;
        case DRAG_RESIZE_T:   ny+=dy; nh-=dy; break;
        case DRAG_RESIZE_B:   nh+=dy; break;
        case DRAG_RESIZE_L:   nx+=dx; nw-=dx; break;
        case DRAG_RESIZE_R:   nw+=dx; break;
        default: break;
    }

    if (nw < 120) nw = 120;
    if (nh < 60)  nh = 60;

    /* Track instantaneous velocity (pixels per frame) for drop bounce */
    g_drag.vel_x = e->x_root - g_drag.prev_x;
    g_drag.vel_y = e->y_root - g_drag.prev_y;
    g_drag.prev_x = e->x_root;
    g_drag.prev_y = e->y_root;

    dc->x = nx; dc->y = ny; dc->w = nw; dc->h = nh;
    XMoveResizeWindow(wm->dpy, dc->frame, nx, ny,
                      (unsigned)nw, (unsigned)nh);
    /* Use runtime cfg for title bar and border dimensions */
    int inner_w = nw - cfg.border_width * 2;
    int inner_h = nh - cfg.title_bar_height - cfg.border_width * 2;
    if (inner_w < 1) inner_w = 1;
    if (inner_h < 1) inner_h = 1;
    XMoveResizeWindow(wm->dpy, dc->win,
                      0, cfg.title_bar_height,
                      (unsigned)inner_w,
                      (unsigned)inner_h);
}

/* ── decorate_button_release ─────────────────────────────── */
void decorate_button_release(void) {
    if (g_drag.mode != DRAG_NONE) {
        /* Trigger drop bounce with the measured release velocity */
        if (g_drag.client && g_drag.mode == DRAG_MOVE && wm->physics_world)
            physics_anim_drop_bounce(wm->physics_world, g_drag.client,
                                      g_drag.vel_x, g_drag.vel_y);

        XUngrabPointer(wm->dpy, CurrentTime);
        g_drag.mode   = DRAG_NONE;
        g_drag.client = NULL;
        g_drag.vel_x  = 0;
        g_drag.vel_y  = 0;
    }
}
