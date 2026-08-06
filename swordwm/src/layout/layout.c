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

/* ── Clamp a window dimension to a sane minimum ─────────── */
static int clamp_dim(int v, int min) { return v < min ? min : v; }

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
        /* Single window: fill entire work area, keep title bar */
        for (Client *c = ws->head; c; c = c->next) {
            if (c->floating || c->fullscreen) continue;
            c->x = ox; c->y = oy; c->w = ow; c->h = oh;
            XMoveResizeWindow(wm->dpy, c->frame,
                              c->x, c->y,
                              (unsigned)c->w, (unsigned)c->h);
            XMoveResizeWindow(wm->dpy, c->win,
                              0, cfg.title_bar_height,
                              (unsigned)clamp_dim(c->w - cfg.border_width * 2, 1),
                              (unsigned)clamp_dim(c->h - cfg.title_bar_height
                                                        - cfg.border_width * 2, 1));
        }
        return;
    }

    /* Master on left half, stack on right half */
    int master_w = ow / 2 - gap / 2;
    int stack_w  = ow - master_w - gap;
    int stack_x  = ox + master_w + gap;
    int stack_n  = n - 1;   /* guaranteed > 0 since n >= 2 */

    int idx = 0;
    for (Client *c = ws->head; c; c = c->next) {
        if (c->floating || c->fullscreen) continue;

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

/* ── layout_monocle: all fullscreen, focused on top ─────── */
static void layout_monocle(Workspace *ws) {
    int ox = cfg.gap_outer;
    int oy = cfg.gap_outer;
    int ow = clamp_dim(wm->sw - cfg.gap_outer * 2, 120);
    int oh = clamp_dim(wm->sh - cfg.gap_outer * 2,  60);

    for (Client *c = ws->head; c; c = c->next) {
        if (c->floating || c->fullscreen) continue;
        c->x = ox; c->y = oy; c->w = ow; c->h = oh;
        XMoveResizeWindow(wm->dpy, c->frame,
                          c->x, c->y,
                          (unsigned)c->w, (unsigned)c->h);
        XMoveResizeWindow(wm->dpy, c->win,
                          0, cfg.title_bar_height,
                          (unsigned)clamp_dim(c->w - cfg.border_width * 2, 1),
                          (unsigned)clamp_dim(c->h - cfg.title_bar_height
                                                    - cfg.border_width * 2, 1));
    }
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
