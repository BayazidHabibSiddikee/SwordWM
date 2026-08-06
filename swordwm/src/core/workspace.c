/* =========================================================
 * src/core/workspace.c — Workspace / virtual desktop management
 * ========================================================= */
#include "swordwm.h"

/* ── workspace_get ───────────────────────────────────────── */
Workspace *workspace_get(int id) {
    for (Workspace *ws = wm->workspaces; ws; ws = ws->next)
        if (ws->id == id) return ws;
    return NULL;
}

/* ── workspace_show — map all clients on a workspace ─────── */
void workspace_show(Workspace *ws) {
    for (Client *c = ws->head; c; c = c->next) {
        XMapWindow(wm->dpy, c->frame);
        XMapWindow(wm->dpy, c->win);
    }
}

/* ── workspace_hide — unmap all clients on a workspace ───── */
void workspace_hide(Workspace *ws) {
    for (Client *c = ws->head; c; c = c->next) {
        XUnmapWindow(wm->dpy, c->win);
        XUnmapWindow(wm->dpy, c->frame);
    }
}

/* ── workspace_switch ────────────────────────────────────── */
void workspace_switch(int id) {
    Workspace *target = workspace_get(id);
    if (!target || target == wm->current_ws) return;

    /* Hide current workspace */
    workspace_hide(wm->current_ws);
    if (wm->focused)
        client_unfocus(wm->focused);
    wm->focused = NULL;

    /* Show target workspace */
    wm->current_ws = target;
    workspace_show(target);
    arrange_workspace(target);

    /* Update _NET_CURRENT_DESKTOP */
    long cur = id;
    XChangeProperty(wm->dpy, wm->root, wm->net_current_desktop,
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&cur, 1);

    /* Focus the previously focused client on this workspace */
    if (target->focused)
        client_focus(target->focused);
    else if (target->head)
        client_focus(target->head);
    else
        client_focus(NULL);

    fprintf(stderr, "swordwm: switched to workspace %d\n", id + 1);
}

/* ── client_move_to_workspace ────────────────────────────── */
void client_move_to_workspace(Client *c, int id) {
    if (!c) return;
    Workspace *target = workspace_get(id);
    if (!target || target == c->ws) return;

    Workspace *old_ws = c->ws;

    /* Unlink from old workspace */
    if (c->prev) c->prev->next = c->next;
    else         old_ws->head  = c->next;
    if (c->next) c->next->prev = c->prev;
    if (old_ws->focused == c) {
        old_ws->focused = c->next ? c->next : c->prev;
    }

    /* Link into new workspace */
    c->prev = NULL;
    c->next = target->head;
    if (target->head) target->head->prev = c;
    target->head = c;
    c->ws = target;

    /* Update _NET_WM_DESKTOP */
    long desktop = id;
    XChangeProperty(wm->dpy, c->win, wm->net_wm_desktop,
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&desktop, 1);

    /* Hide window if target workspace is not active */
    if (target != wm->current_ws) {
        XUnmapWindow(wm->dpy, c->win);
        XUnmapWindow(wm->dpy, c->frame);
    }

    /* Re-arrange both workspaces */
    arrange_workspace(old_ws);
    if (target == wm->current_ws)
        arrange_workspace(target);

    /* Focus something on the old workspace */
    if (wm->focused == c) {
        if (old_ws->focused)
            client_focus(old_ws->focused);
        else
            client_focus(NULL);
    }

    fprintf(stderr, "swordwm: moved window \"%s\" to workspace %d\n",
            c->title, id + 1);
}
