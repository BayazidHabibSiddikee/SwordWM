/* =========================================================
 * src/input/input.c — keyboard bindings and WM actions
 * All action functions share the same signature: void(const char *)
 * ========================================================= */
#include "swordwm.h"
#include "config_parser.h"
#include <stdlib.h>
#include <ctype.h>

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
    /* Fall back to configured terminal if no command given */
    const char *run = (cmd && cmd[0]) ? cmd : cfg.terminal;
    if (!run || !run[0]) return;

    if (fork() == 0) {
        setsid();
        /*
         * Use execv with an explicit argv array — avoids shell injection.
         * We pass the command string to /bin/sh -c but as a fixed positional
         * argument, not as a shell-interpolated string built by the caller.
         * The only way to avoid /bin/sh entirely would be to tokenise the
         * command here, but config-file commands legitimately need shell
         * features (pipes, redirects, env vars).  Using execv instead of
         * execlp prevents the shell from being searched via PATH injection.
         */
        char *argv[] = { "/bin/sh", "-c", (char *)run, NULL };
        execv("/bin/sh", argv);
        fprintf(stderr, "swordwm: exec failed: %s\n", run);
        _exit(1);
    }
}

/* ── action_close_focused ────────────────────────────────── */
void action_close_focused(const char *arg) {
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
        /* Use runtime cfg values for title bar and border, not compile-time
         * constants — these can differ after a config reload. */
        int inner_w = c->w - cfg.border_width * 2;
        int inner_h = c->h - cfg.title_bar_height - cfg.border_width * 2;
        if (inner_w < 1) inner_w = 1;
        if (inner_h < 1) inner_h = 1;
        XMoveResizeWindow(wm->dpy, c->win,
                          0, cfg.title_bar_height,
                          (unsigned)inner_w,
                          (unsigned)inner_h);
        XRaiseWindow(wm->dpy, c->frame);
    } else {
        c->old_x = c->x; c->old_y = c->y;
        c->old_w = c->w; c->old_h = c->h;
    }

    /* Update physics body flags for floating/tiled state change */
    if (wm->physics_world) {
        PhysicsBody *body = physics_find_body(wm->physics_world, c->win);
        if (body) {
            body->tiled = !c->floating;
            body->fullscreen = c->fullscreen;
        }
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
    Workspace *ws = wm->current_ws;
    if (ws->gap < MAX_GAP)
        ws->gap += 4;
    arrange_workspace(ws);
}

void action_gap_dec(const char *arg) {
    (void)arg;
    Workspace *ws = wm->current_ws;
    if (ws->gap > 0)
        ws->gap = (ws->gap >= 4) ? ws->gap - 4 : 0;
    arrange_workspace(ws);
}

/* ── action_quit ─────────────────────────────────────────── */
void action_quit(const char *arg) {
    (void)arg;
    wm->running = 0;
}

/* ── action_workspace_prev ────────────────────────────────── */
void action_workspace_prev(const char *arg) {
    (void)arg;
    Workspace *ws = wm->current_ws;
    if (!ws) return;

    /* Find the previous workspace in the linked list (wrap around) */
    Workspace *prev = wm->workspaces;
    Workspace *iter = wm->workspaces;
    while (iter && iter != ws) {
        prev = iter;
        iter = iter->next;
    }
    workspace_switch(prev->id);
}

/* ── action_workspace_next ────────────────────────────────── */
void action_workspace_next(const char *arg) {
    (void)arg;
    Workspace *ws = wm->current_ws;
    if (!ws || !ws->next) return;

    workspace_switch(ws->next->id);
}

/* ── action_switch_workspace ─────────────────────────────── */
void action_switch_workspace(const char *id) {
    if (!id) return;
    
    /* Validate input is numeric */
    if (!isdigit((unsigned char)id[0])) {
        fprintf(stderr, "swordwm: workspace '%s' is not numeric, ignored\n", id);
        return;
    }
    
    int n = atoi(id);
    if (n < 0 || n >= wm->num_workspaces) {
        fprintf(stderr, "swordwm: workspace %d out of range [0,%d], ignored\n",
                n, wm->num_workspaces - 1);
        return;
    }
    workspace_switch(n);
}

/* ── action_move_to_workspace ────────────────────────────── */
void action_move_to_workspace(const char *id) {
    if (!id || !wm->focused) return;
    
    /* Validate input is numeric */
    if (!isdigit((unsigned char)id[0])) {
        fprintf(stderr, "swordwm: workspace '%s' is not numeric, ignored\n", id);
        return;
    }
    
    int n = atoi(id);
    if (n < 0 || n >= wm->num_workspaces) {
        fprintf(stderr, "swordwm: workspace %d out of range [0,%d], ignored\n",
                n, wm->num_workspaces - 1);
        return;
    }
    client_move_to_workspace(wm->focused, n);
}

/* ── action_reload_config ────────────────────────────────── */
void action_reload_config(const char *arg) {
    (void)arg;
    config_reload();
    action_spawn("~/SwordWM/cyberdesk.sh restart");
}

/* ── action_move_stack_up ────────────────────────────────── */
/* Swap the focused window with the one before it in the list */
void action_move_stack_up(const char *arg) {
    (void)arg;
    Workspace *ws = wm->current_ws;
    Client *c = wm->focused;
    if (!c || !c->prev) return;   /* already at top */

    Client *prev = c->prev;

    /* Detach c */
    if (c->next) c->next->prev = prev;
    prev->next = c->next;

    /* Insert c before prev */
    c->next = prev;
    c->prev = prev->prev;
    if (prev->prev) prev->prev->next = c;
    else            ws->head = c;
    prev->prev = c;

    arrange_workspace(ws);
}

/* ── action_move_stack_down ──────────────────────────────── */
/* Swap the focused window with the one after it in the list */
void action_move_stack_down(const char *arg) {
    (void)arg;
    Workspace *ws = wm->current_ws;
    Client *c = wm->focused;
    if (!c || !c->next) return;   /* already at bottom */

    Client *next = c->next;

    /* Detach c */
    if (c->prev) c->prev->next = next;
    else         ws->head = next;
    next->prev = c->prev;

    /* Insert c after next */
    c->prev = next;
    c->next = next->next;
    if (next->next) next->next->prev = c;
    next->next = c;

    arrange_workspace(ws);
}

/* ── action_master_grow / action_master_shrink ───────────── */
void action_master_grow(const char *arg) {
    (void)arg;
    Workspace *ws = wm->current_ws;
    if (ws->master_ratio < 90)
        ws->master_ratio += 5;
    arrange_workspace(ws);
}

void action_master_shrink(const char *arg) {
    (void)arg;
    Workspace *ws = wm->current_ws;
    if (ws->master_ratio > 10)
        ws->master_ratio -= 5;
    arrange_workspace(ws);
}

/* ── action_minimize — toggle _NET_WM_STATE_HIDDEN ───────── */
void action_minimize(const char *arg) {
    (void)arg;
    Client *c = wm->focused;
    if (!c) return;

    Atom state_atom   = wm->net_wm_state;
    Atom hidden_atom  = XInternAtom(wm->dpy, "_NET_WM_STATE_HIDDEN", False);
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type            = ClientMessage;
    ev.xclient.window  = c->win;
    ev.xclient.message_type = state_atom;
    ev.xclient.format  = 32;
    ev.xclient.data.l[0]  = 2;  /* _NET_WM_STATE_TOGGLE */
    ev.xclient.data.l[1]  = (long)hidden_atom;
    ev.xclient.data.l[2]  = 0;
    ev.xclient.data.l[3]  = 0;
    ev.xclient.data.l[4]  = 0;
    XSendEvent(wm->dpy, DefaultRootWindow(wm->dpy), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    XFlush(wm->dpy);
    fprintf(stderr, "swordwm: minimize %s\n", c->title);
}

/* ── action_vol_up / action_vol_down / action_mute ───────── */
void action_vol_up(const char *arg) {
    (void)arg;
    action_spawn("pactl set-sink-volume @DEFAULT_SINK@ +5%");
}

void action_vol_down(const char *arg) {
    (void)arg;
    action_spawn("pactl set-sink-volume @DEFAULT_SINK@ -5%");
}

void action_mute(const char *arg) {
    (void)arg;
    action_spawn("pactl set-sink-mute @DEFAULT_SINK@ toggle");
}

/* ── action_bright_up / action_bright_down ────────────────── */
void action_bright_up(const char *arg) {
    (void)arg;
    action_spawn("brightnessctl s +5%");
}

void action_bright_down(const char *arg) {
    (void)arg;
    action_spawn("brightnessctl s 5%-");
}
