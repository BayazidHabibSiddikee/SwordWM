/* =========================================================
 * wobble.c — anime spring physics for SwordWM window geometry
 *
 * HOW IT WORKS (no compositor required):
 *
 *   Every managed floating window carries a WobbleState.
 *   Each frame (driven by pselect timeout in main.c) we:
 *     1. Advance the spring simulation for each active client
 *     2. Call XMoveResizeWindow with the animated geometry
 *
 *   Spring model: damped harmonic oscillator
 *     a = k*(target - pos) - d*vel
 *     vel += a
 *     pos += vel
 *
 *   k  (stiffness) controls how fast it snaps back
 *   d  (damping)   controls how quickly oscillation fades
 *
 *   "Anime bubbly" feel:
 *     k=0.18  d=0.72  → big slow gooey blob
 *     k=0.30  d=0.80  → snappy jelly (Compiz-like)
 *     k=0.12  d=0.65  → very slow watery, lots of bounce
 *
 *   SQUASH & STRETCH on drag release:
 *     When released with horizontal velocity → squeeze width,
 *     exaggerate height (like a blob landing).
 *     Spring releases squeeze back to 1.0 on the same oscillator.
 * ========================================================= */
#include "swordwm.h"
#include "wobble.h"
#include "config_parser.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* ── Tuning ──────────────────────────────────────────────── */
#define K_POS      0.22   /* position spring stiffness          */
#define D_POS      0.76   /* position damping (0=no damp, 1=dead)*/
#define K_SIZE     0.18   /* size spring stiffness              */
#define D_SIZE     0.72   /* size damping                       */
#define K_SQUEEZE  0.20   /* squash/stretch spring stiffness    */
#define D_SQUEEZE  0.68   /* squash/stretch damping             */

#define SETTLE_EPS 0.6    /* pixel threshold to declare settled */
#define MIN_SQUEEZE_VEL 8.0  /* minimum velocity to trigger squash */

/* ── State pool ──────────────────────────────────────────── */
/* One WobbleState per slot in wm->all_clients[512] */
static WobbleState s_states[512];

/* ─────────────────────────────────────────────────────────── */

static int client_index(Client *c) {
    for (int i = 0; i < wm->num_clients; i++)
        if (wm->all_clients[i] == c) return i;
    return -1;
}

static WobbleState *state_for(Client *c) {
    int i = client_index(c);
    if (i < 0 || i >= 512) return NULL;
    return &s_states[i];
}

/* ── wobble_init ─────────────────────────────────────────── */
void wobble_init(Client *c) {
    WobbleState *s = state_for(c);
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->ax = s->tx = c->x;
    s->ay = s->ty = c->y;
    s->aw = s->tw = c->w;
    s->ah = s->th = c->h;
    s->squeeze = 0.0;
    s->active  = 0;
}

/* ── wobble_destroy ──────────────────────────────────────── */
void wobble_destroy(Client *c) {
    WobbleState *s = state_for(c);
    if (s) memset(s, 0, sizeof(*s));
}

/* ── wobble_map_bounce ───────────────────────────────────── */
/* Window pops in: start 8% smaller, offset slightly upward,
 * let the spring overshoot to full size. */
void wobble_map_bounce(Client *c) {
    if (!c->floating) return;
    WobbleState *s = state_for(c);
    if (!s) return;

    s->tx = c->x;  s->ty = c->y;
    s->tw = c->w;  s->th = c->h;

    double shrink = 0.08;
    s->ax = c->x + c->w * shrink * 0.5;
    s->ay = c->y + c->h * shrink * 0.5 + 12;
    s->aw = c->w * (1.0 - shrink);
    s->ah = c->h * (1.0 - shrink);

    /* give it a little upward velocity for a "pop" feel */
    s->vx = 0;  s->vy = -3.0;
    s->vw = 0;  s->vh = 0;
    s->squeeze = 0.06;   /* start slightly squeezed vertically */
    s->squeeze_vel = 0;
    s->active = 1;
}

/* ── wobble_drop_bounce ──────────────────────────────────── */
/* Call on drag release. vel_x/vel_y = pixels/frame at release. */
void wobble_drop_bounce(Client *c, double vel_x, double vel_y) {
    if (!c->floating) return;
    WobbleState *s = state_for(c);
    if (!s) return;

    s->tx = c->x;  s->ty = c->y;
    s->tw = c->w;  s->th = c->h;
    /* Keep current animated pos as starting point */
    s->vx = vel_x * 0.35;
    s->vy = vel_y * 0.35;

    /* Squash/stretch: fast horizontal → squeeze width + bulge height */
    double speed = sqrt(vel_x*vel_x + vel_y*vel_y);
    if (speed > MIN_SQUEEZE_VEL) {
        double axis = vel_x / (speed + 0.001);  /* -1=horiz, approaching 0=vertical */
        /* squeeze < 0 means we compressed width and expanded height */
        s->squeeze     = -axis * (speed / 120.0) * 0.18;
        s->squeeze     = s->squeeze >  0.20 ?  0.20 : s->squeeze;
        s->squeeze     = s->squeeze < -0.20 ? -0.20 : s->squeeze;
        s->squeeze_vel = 0;
    }
    s->active = 1;
}

/* ── Apply animated geometry to an actual X window ──────── */
static void apply_geometry(Client *c, WobbleState *s) {
    /* Apply squash/stretch: squeeze factor modifies w and h inversely */
    double sw = s->aw * (1.0 + s->squeeze * 0.5);
    double sh = s->ah * (1.0 - s->squeeze * 0.3);

    /* Clamp to sane minimums */
    if (sw < 80)  sw = 80;
    if (sh < 40)  sh = 40;

    int nx = (int)(s->ax);
    int ny = (int)(s->ay);
    int nw = (int)(sw);
    int nh = (int)(sh);

    /* Don't waste a syscall if geometry hasn't changed */
    if (nx == c->x && ny == c->y && nw == c->w && nh == c->h) return;

    c->x = nx; c->y = ny; c->w = nw; c->h = nh;
    XMoveResizeWindow(wm->dpy, c->frame, nx, ny,
                      (unsigned)nw, (unsigned)nh);
    int inner_w = nw - cfg.border_width * 2;
    int inner_h = nh - cfg.title_bar_height - cfg.border_width * 2;
    if (inner_w < 1) inner_w = 1;
    if (inner_h < 1) inner_h = 1;
    XMoveResizeWindow(wm->dpy, c->win, 0, cfg.title_bar_height,
                      (unsigned)inner_w, (unsigned)inner_h);
}

/* ── Spring step for one scalar ──────────────────────────── */
static void spring_step(double *pos, double *vel,
                        double target, double k, double d) {
    double a = k * (target - *pos) - d * (*vel);
    *vel += a;
    *pos += *vel;
}

/* ── wobble_step_all ─────────────────────────────────────── */
int wobble_step_all(void) {
    int active = 0;
    for (int i = 0; i < wm->num_clients; i++) {
        Client *c = wm->all_clients[i];
        if (!c) continue;
        WobbleState *s = &s_states[i];
        if (!s->active) continue;

        /* If the window became tiled (e.g. arrange_workspace moved it),
         * snap immediately — no animation for tiled windows. */
        if (!c->floating) {
            s->ax = c->x; s->ay = c->y;
            s->aw = c->w; s->ah = c->h;
            s->vx = s->vy = s->vw = s->vh = 0;
            s->squeeze = s->squeeze_vel = 0;
            s->active = 0;
            continue;
        }

        /* NOTE: Do NOT read c->x/y/w/h here as the target.
         * apply_geometry() writes animated values back into c->x/y/w/h,
         * so reading them would overwrite the real target with the
         * animated position — causing windows to get stuck smaller.
         * Targets are set only in wobble_map_bounce / wobble_drop_bounce
         * and updated by the settle-snap at the end of this function. */

        /* Step position springs */
        spring_step(&s->ax, &s->vx, s->tx, K_POS,  D_POS);
        spring_step(&s->ay, &s->vy, s->ty, K_POS,  D_POS);
        /* Step size springs */
        spring_step(&s->aw, &s->vw, s->tw, K_SIZE, D_SIZE);
        spring_step(&s->ah, &s->vh, s->th, K_SIZE, D_SIZE);
        /* Step squash spring back to 0 */
        spring_step(&s->squeeze, &s->squeeze_vel, 0.0, K_SQUEEZE, D_SQUEEZE);

        apply_geometry(c, s);

        /* Check if settled */
        if (fabs(s->ax - s->tx) < SETTLE_EPS &&
            fabs(s->ay - s->ty) < SETTLE_EPS &&
            fabs(s->aw - s->tw) < SETTLE_EPS &&
            fabs(s->ah - s->th) < SETTLE_EPS &&
            fabs(s->vx) < SETTLE_EPS && fabs(s->vy) < SETTLE_EPS &&
            fabs(s->vw) < SETTLE_EPS && fabs(s->vh) < SETTLE_EPS &&
            fabs(s->squeeze) < 0.005) {
            /* Snap to exact target and deactivate */
            s->ax = s->tx; s->ay = s->ty;
            s->aw = s->tw; s->ah = s->th;
            s->squeeze = 0;
            s->active = 0;
            /* Final snap to exact geometry */
            c->x = (int)s->tx; c->y = (int)s->ty;
            c->w = (int)s->tw; c->h = (int)s->th;
            XMoveResizeWindow(wm->dpy, c->frame,
                              c->x, c->y, (unsigned)c->w, (unsigned)c->h);
            int iw = c->w - cfg.border_width * 2;
            int ih = c->h - cfg.title_bar_height - cfg.border_width * 2;
            if (iw < 1) iw = 1;
            if (ih < 1) ih = 1;
            XMoveResizeWindow(wm->dpy, c->win, 0, cfg.title_bar_height,
                              (unsigned)iw, (unsigned)ih);
        } else {
            active++;
        }
    }
    if (active > 0) XFlush(wm->dpy);
    return active;
}

/* ── wobble_any_active ───────────────────────────────────── */
int wobble_any_active(void) {
    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->all_clients[i] && s_states[i].active)
            return 1;
    }
    return 0;
}
