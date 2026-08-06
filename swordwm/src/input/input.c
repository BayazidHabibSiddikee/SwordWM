/* =========================================================
 * src/input/input.c — keyboard bindings and WM actions
 * All action functions share the same signature: void(const char *)
 * ========================================================= */
#include "swordwm.h"
#include "config_parser.h"
#include <stdlib.h>

/* ── Keybinding table from config.h ──────────────────────── */
static const KeyBinding keybindings[] = { KEYBINDINGS };
static const int n_keybindings =
    (int)(sizeof(keybindings) / sizeof(keybindings[0]));

/* ── keybind_process ─────────────────────────────────────── */
void keybind_process(XKeyEvent *e) {
    unsigned int clean = e->state & ~(LockMask | Mod2Mask);
    KeySym sym = XkbKeycodeToKeysym(wm->dpy, (KeyCode)e->keycode, 0, 0);

    /* Check runtime config binds first */
    for (int i = 0; i < cfg.n_binds; i++) {
        if (sym == cfg.binds[i].keysym &&
            clean == cfg.binds[i].modmask &&
            cfg.binds[i].action) {
            cfg.binds[i].action(cfg.binds[i].arg);
            return;
        }
    }

    /* Fall back to compiled-in keybindings */
    for (int i = 0; i < n_keybindings; i++) {
        if (sym == keybindings[i].keysym &&
            clean == keybindings[i].modmask) {
            keybindings[i].action(keybindings[i].arg);
            return;
        }
    }
}

/* ── action_spawn ────────────────────────────────────────── */
void action_spawn(const char *cmd) {
    /* If no command given, fall back to configured terminal */
    const char *run = (cmd && cmd[0]) ? cmd : cfg.terminal;
    if (!run || !run[0]) return;
    if (fork() == 0) {
        setsid();
        execlp("/bin/sh", "sh", "-c", run, NULL);
        fprintf(stderr, "swordwm: exec failed: %s\n", run);
        exit(1);
    }
}

/* ── action_close_window ─────────────────────────────────── */
void action_close_window(const char *arg) {
    (void)arg;
    Client *c = wm->focused;
    if (!c) return;

    /* Try WM_DELETE_WINDOW protocol first (graceful) */
    int n;
    Atom *protocols = NULL;
    if (XGetWMProtocols(wm->dpy, c->win, &protocols, &n)) {
        for (int i = 0; i < n; i++) {
            if (protocols[i] == wm->wm_delete_window) {
                XEvent ev;
                ev.type                 = ClientMessage;
                ev.xclient.window       = c->win;
                ev.xclient.message_type = wm->wm_protocols;
                ev.xclient.format       = 32;
                ev.xclient.data.l[0]    = (long)wm->wm_delete_window;
                ev.xclient.data.l[1]    = CurrentTime;
                XSendEvent(wm->dpy, c->win, False, NoEventMask, &ev);
                XFree(protocols);
                return;
            }
        }
        XFree(protocols);
    }

    /* Fall back to XKillClient */
    XKillClient(wm->dpy, c->win);
}

/* ── action_focus_next ───────────────────────────────────── */
void action_focus_next(const char *arg) {
    (void)arg;
    Workspace *ws = wm->current_ws;
    if (!ws->head) return;

    Client *next = wm->focused && wm->focused->next
                   ? wm->focused->next
                   : ws->head;
    client_focus(next);
}

/* ── action_focus_prev ───────────────────────────────────── */
void action_focus_prev(const char *arg) {
    (void)arg;
    Workspace *ws = wm->current_ws;
    if (!ws->head) return;

    if (!wm->focused || !wm->focused->prev) {
        Client *last = ws->head;
        while (last->next) last = last->next;
        client_focus(last);
    } else {
        client_focus(wm->focused->prev);
    }
}

/* ── action_toggle_floating ──────────────────────────────── */
void action_toggle_floating(const char *arg) {
    (void)arg;
    Client *c = wm->focused;
    if (!c) return;

    c->floating = !c->floating;

    if (c->floating) {
        if (c->old_w > 0) {
            c->x = c->old_x; c->y = c->old_y;
            c->w = c->old_w; c->h = c->old_h;
        } else {
            c->w = wm->sw * 60 / 100;
            c->h = wm->sh * 60 / 100;
            c->x = (wm->sw - c->w) / 2;
            c->y = (wm->sh - c->h) / 2;
        }
        XMoveResizeWindow(wm->dpy, c->frame,
                          c->x, c->y, c->w, c->h);
        XMoveResizeWindow(wm->dpy, c->win,
                          0, TITLE_BAR_HEIGHT,
                          c->w - BORDER_WIDTH * 2,
                          c->h - TITLE_BAR_HEIGHT - BORDER_WIDTH * 2);
        XRaiseWindow(wm->dpy, c->frame);
    } else {
        c->old_x = c->x; c->old_y = c->y;
        c->old_w = c->w; c->old_h = c->h;
    }

    arrange_workspace(c->ws);
}

/* ── action_rotate_layout ────────────────────────────────── */
void action_rotate_layout(const char *arg) {
    (void)arg;
    Workspace *ws = wm->current_ws;
    ws->layout = (Layout)((ws->layout + 1) % LAYOUT_COUNT);

    const char *names[] = { "tile", "monocle", "float" };
    fprintf(stderr, "swordwm: layout → %s\n", names[ws->layout]);
    arrange_workspace(ws);
}

/* ── action_gap_inc / action_gap_dec ─────────────────────── */
void action_gap_inc(const char *arg) {
    (void)arg;
    wm->current_ws->gap += 4;
    arrange_workspace(wm->current_ws);
}

void action_gap_dec(const char *arg) {
    (void)arg;
    if (wm->current_ws->gap >= 4)
        wm->current_ws->gap -= 4;
    arrange_workspace(wm->current_ws);
}

/* ── action_quit ─────────────────────────────────────────── */
void action_quit(const char *arg) {
    (void)arg;
    wm->running = 0;
}

/* ── action_switch_workspace ─────────────────────────────── */
void action_switch_workspace(const char *id) {
    if (!id) return;
    workspace_switch(atoi(id));
}

/* ── action_move_to_workspace ────────────────────────────── */
void action_move_to_workspace(const char *id) {
    if (!id || !wm->focused) return;
    client_move_to_workspace(wm->focused, atoi(id));
}

/* ── action_reload_config ────────────────────────────────── */
void action_reload_config(const char *arg) {
    (void)arg;
    config_reload();
}
