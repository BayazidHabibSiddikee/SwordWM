/* =========================================================
 * src/layout/layout.c — tiling, monocle and floating layout engine
 * ========================================================= */
#include "swordwm.h"

/* ── Count tiled (non-floating, non-fullscreen) clients ──── */
static int count_tiled(Workspace *ws) {
    int n = 0;
    for (Client *c = ws->head; c; c = c->next)
        if (!c->floating && !c->fullscreen) n++;
    return n;
}

/* ── layout_tile: master/stack ───────────────────────────── */
static void layout_tile(Workspace *ws) {
    int n = count_tiled(ws);
    if (n == 0) return;

    int gap  = ws->gap;
    int ox   = GAP_OUTER;
    int oy   = GAP_OUTER;
    int ow   = wm->sw - GAP_OUTER * 2;
    int oh   = wm->sh - GAP_OUTER * 2;

    if (n == 1) {
        /* Single window: fill entire area */
        for (Client *c = ws->head; c; c = c->next) {
            if (c->floating || c->fullscreen) continue;
            int fw = ow;
            int fh = oh;
            c->x = ox; c->y = oy; c->w = fw; c->h = fh;
            XMoveResizeWindow(wm->dpy, c->frame,
                              c->x, c->y, c->w, c->h);
            XMoveResizeWindow(wm->dpy, c->win,
                              0, TITLE_BAR_HEIGHT,
                              c->w - BORDER_WIDTH * 2,
                              c->h - TITLE_BAR_HEIGHT - BORDER_WIDTH * 2);
        }
        return;
    }

    /* Master on left half, stack on right half */
    int master_w = ow / 2 - gap / 2;
    int stack_w  = ow - master_w - gap;
    int stack_x  = ox + master_w + gap;
    int stack_n  = n - 1;

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
            /* Stack windows */
            int each_h = (oh - gap * (stack_n - 1)) / stack_n;
            int pos     = idx - 1;
            c->x = stack_x;
            c->y = oy + pos * (each_h + gap);
            c->w = stack_w;
            c->h = each_h;
        }

        XMoveResizeWindow(wm->dpy, c->frame,
                          c->x, c->y, c->w, c->h);
        XMoveResizeWindow(wm->dpy, c->win,
                          0, TITLE_BAR_HEIGHT,
                          c->w - BORDER_WIDTH * 2,
                          c->h - TITLE_BAR_HEIGHT - BORDER_WIDTH * 2);
        idx++;
    }
}

/* ── layout_monocle: all fullscreen, focused on top ─────── */
static void layout_monocle(Workspace *ws) {
    int ox = GAP_OUTER;
    int oy = GAP_OUTER;
    int ow = wm->sw - GAP_OUTER * 2;
    int oh = wm->sh - GAP_OUTER * 2;

    for (Client *c = ws->head; c; c = c->next) {
        if (c->floating || c->fullscreen) continue;
        c->x = ox; c->y = oy; c->w = ow; c->h = oh;
        XMoveResizeWindow(wm->dpy, c->frame,
                          c->x, c->y, c->w, c->h);
        XMoveResizeWindow(wm->dpy, c->win,
                          0, TITLE_BAR_HEIGHT,
                          c->w - BORDER_WIDTH * 2,
                          c->h - TITLE_BAR_HEIGHT - BORDER_WIDTH * 2);
    }
    /* Raise the focused window on top */
    if (ws->focused)
        XRaiseWindow(wm->dpy, ws->focused->frame);
}

/* ── layout_floating: no rearrangement ──────────────────── */
static void layout_floating(Workspace *ws) {
    /* Nothing to do — windows stay wherever they were placed */
    (void)ws;
}

/* ── arrange_workspace ───────────────────────────────────── */
void arrange_workspace(Workspace *ws) {
    if (!ws) return;

    /* Handle fullscreen clients first — they cover everything */
    for (Client *c = ws->head; c; c = c->next) {
        if (c->fullscreen) {
            XMoveResizeWindow(wm->dpy, c->frame,
                              0, 0, wm->sw, wm->sh);
            XMoveResizeWindow(wm->dpy, c->win,
                              0, 0, wm->sw, wm->sh);
            XRaiseWindow(wm->dpy, c->frame);
        }
    }

    /* Run the layout for non-fullscreen tiled windows */
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
