/* =========================================================
 * src/layout/layout.c — tiling, monocle and floating layout engine
 * ========================================================= */
#include "swordwm.h"
#include "config_parser.h"

/* ── Count tiled (non-floating, non-fullscreen) clients ──── */
static int count_tiled(Workspace *ws) {
    int n = 0;
    for (Client *c = ws->head; c; c = c->next)
        if (!c->floating && !c->fullscreen) n++;
    return n;
}

/* ── Predicate: true for clients that participate in tiling ─ */
static int client_is_tiled(Client *c) {
    return !c->floating && !c->fullscreen;
}

/* ── Iterate tiled clients, calling fn(c, userdata) for each ─
 * Abstracts the repeated "skip floating/fullscreen" filter.   */
static void for_each_tiled(Workspace *ws,
                            void (*fn)(Client *, void *),
                            void *userdata) {
    for (Client *c = ws->head; c; c = c->next)
        if (client_is_tiled(c))
            fn(c, userdata);
}

/* ── Clamp a window dimension to a sane minimum ─────────── */
static int clamp_dim(int v, int min) { return v < min ? min : v; }

/* ── Clamp frame size to client's WM_NORMAL_HINTS min size ─ */
static void clamp_to_hints(Client *c) {
    if (c->min_w > 0 && c->w < c->min_w) c->w = c->min_w;
    if (c->min_h > 0 && c->h < c->min_h) c->h = c->min_h;
}

/* ── place_single: callback used by for_each_tiled ──────── */
typedef struct { int ox, oy, ow, oh; } TileSingleArgs;

static void place_single(Client *c, void *data) {
    TileSingleArgs *a = (TileSingleArgs *)data;
    c->x = a->ox; c->y = a->oy; c->w = a->ow; c->h = a->oh;
    clamp_to_hints(c);
    XMoveResizeWindow(wm->dpy, c->frame,
                      c->x, c->y,
                      (unsigned)c->w, (unsigned)c->h);
    XMoveResizeWindow(wm->dpy, c->win,
                      0, cfg.title_bar_height,
                      (unsigned)clamp_dim(c->w - cfg.border_width * 2, 1),
                      (unsigned)clamp_dim(c->h - cfg.title_bar_height
                                                - cfg.border_width * 2, 1));
}

/* ── layout_tile: master/stack ───────────────────────────── */
static void layout_tile(Workspace *ws) {
    int n = count_tiled(ws);
    if (n == 0) return;

    int gap  = ws->gap;
    int ox   = cfg.gap_outer;
    int oy   = cfg.gap_outer;
    int ow   = clamp_dim(wm->sw - cfg.gap_outer * 2, 120);
    int oh   = clamp_dim(wm->sh - cfg.gap_outer * 2,  60);

    if (n == 1) {
        /* Single window: fill entire work area via for_each_tiled */
        TileSingleArgs a = { ox, oy, ow, oh };
        for_each_tiled(ws, place_single, &a);
        return;
    }

    /* Master on left, stack on right — ratio from per-workspace setting */
    int ratio    = (ws->master_ratio > 0) ? ws->master_ratio : 50;
    int master_w = ow * ratio / 100 - gap / 2;
    int stack_w  = ow - master_w - gap;
    int stack_x  = ox + master_w + gap;
    int stack_n  = n - 1;   /* guaranteed > 0 since n >= 2 */

    int idx = 0;
    for (Client *c = ws->head; c; c = c->next) {
        if (!client_is_tiled(c)) continue;

        if (idx == 0) {
            /* Master window */
            c->x = ox;
            c->y = oy;
            c->w = master_w;
            c->h = oh;
        } else {
            /* Stack windows — stack_n > 0 guaranteed */
            int each_h = clamp_dim(
                (oh - gap * (stack_n - 1)) / stack_n, 60);
            int pos = idx - 1;
            c->x = stack_x;
            c->y = oy + pos * (each_h + gap);
            c->w = stack_w;
            c->h = each_h;
        }

        clamp_to_hints(c);
        XMoveResizeWindow(wm->dpy, c->frame,
                          c->x, c->y,
                          (unsigned)c->w, (unsigned)c->h);
        XMoveResizeWindow(wm->dpy, c->win,
                          0, cfg.title_bar_height,
                          (unsigned)clamp_dim(c->w - cfg.border_width * 2, 1),
                          (unsigned)clamp_dim(c->h - cfg.title_bar_height
                                                    - cfg.border_width * 2, 1));
        idx++;
    }
}

/* ── place_monocle: callback used by for_each_tiled ─────── */
typedef struct { int ox, oy, ow, oh; } MonocleArgs;

static void place_monocle(Client *c, void *data) {
    MonocleArgs *a = (MonocleArgs *)data;
    c->x = a->ox; c->y = a->oy; c->w = a->ow; c->h = a->oh;
    clamp_to_hints(c);
    XMoveResizeWindow(wm->dpy, c->frame,
                      c->x, c->y,
                      (unsigned)c->w, (unsigned)c->h);
    XMoveResizeWindow(wm->dpy, c->win,
                      0, cfg.title_bar_height,
                      (unsigned)clamp_dim(c->w - cfg.border_width * 2, 1),
                      (unsigned)clamp_dim(c->h - cfg.title_bar_height
                                                - cfg.border_width * 2, 1));
}

/* ── layout_monocle: all fullscreen, focused on top ─────── */
static void layout_monocle(Workspace *ws) {
    int ox = cfg.gap_outer;
    int oy = cfg.gap_outer;
    int ow = clamp_dim(wm->sw - cfg.gap_outer * 2, 120);
    int oh = clamp_dim(wm->sh - cfg.gap_outer * 2,  60);

    MonocleArgs a = { ox, oy, ow, oh };
    for_each_tiled(ws, place_monocle, &a);

    /* Raise the focused window on top */
    if (ws->focused)
        XRaiseWindow(wm->dpy, ws->focused->frame);
}

/* ── layout_floating: no rearrangement ──────────────────── */
static void layout_floating(Workspace *ws) {
    (void)ws; /* Windows stay wherever they were placed */
}

/* ── arrange_workspace ───────────────────────────────────── */
void arrange_workspace(Workspace *ws) {
    if (!ws) return;

    /* Handle fullscreen clients — they cover everything */
    for (Client *c = ws->head; c; c = c->next) {
        if (c->fullscreen) {
            XMoveResizeWindow(wm->dpy, c->frame,
                              0, 0,
                              (unsigned)wm->sw, (unsigned)wm->sh);
            XMoveResizeWindow(wm->dpy, c->win,
                              0, 0,
                              (unsigned)wm->sw, (unsigned)wm->sh);
            XRaiseWindow(wm->dpy, c->frame);
        }
    }

    switch (ws->layout) {
        case LAYOUT_TILE:    layout_tile(ws);    break;
        case LAYOUT_MONOCLE: layout_monocle(ws); break;
        case LAYOUT_FLOAT:   layout_floating(ws);break;
        default:             layout_tile(ws);    break;
    }

    /* Raise floating windows above tiled */
    for (Client *c = ws->head; c; c = c->next) {
        if (c->floating && !c->fullscreen)
            XRaiseWindow(wm->dpy, c->frame);
    }

    XSync(wm->dpy, False);
}
