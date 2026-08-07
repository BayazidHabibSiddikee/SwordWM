/* =========================================================
 * wobble.c — advanced spring physics for SwordWM
 *
 * BETTER THAN WOBBLY:
 *   - Multi-bounce settling (not just one overshoot)
 *   - Inertia/momentum after release
 *   - Edge bounce off screen boundaries
 *   - Rotation wobble during fast movement
 *   - Corner lag for jelly-like deformation
 *   - Wave propagation through window body
 *   - Velocity-dependent squash and stretch
 *   - Gravity effect during free movement
 *
 * No compositor needed — animates XMoveResizeWindow directly.
 * ========================================================= */
#include "swordwm.h"
#include "wobble.h"
#include "config_parser.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* ── Tuning constants ────────────────────────────────────── */
/* Position spring */
#define K_POS           0.25    /* stiffness */
#define D_POS           0.78    /* damping */
/* Size spring */
#define K_SIZE          0.20
#define D_SIZE          0.74
/* Rotation spring */
#define K_ROT           0.15
#define D_ROT           0.70
/* Squash/stretch */
#define K_SQUEEZE       0.22
#define D_SQUEEZE       0.65
/* Corner lag (jelly) */
#define K_CORNER        0.12    /* softer = more jelly */
#define D_CORNER        0.60
/* Inertia */
#define INERTIA_FRICTION 0.92   /* velocity multiplier per frame */
#define INERTIA_MIN     0.3     /* stop below this speed */
/* Edge bounce */
#define EDGE_BOUNCE_K   0.35    /* how snappy the bounce is */
#define EDGE_BOUNCE_D   0.70
#define EDGE_PENETRATION 0.4    /* how deep before bounce triggers */
/* Multi-bounce */
#define MAX_BOUNCES     4       /* max bounces before forced settle */
#define BOUNCE_DECAY    0.55    /* energy retained per bounce */
/* Wave */
#define WAVE_SPEED      0.15    /* phase advance per frame */
#define WAVE_DECAY      0.97    /* amplitude decay per frame */
/* Gravity */
#define GRAVITY         0.12    /* downward acceleration when floating */
/* Thresholds */
#define SETTLE_EPS      0.5
#define MIN_SQUEEZE_VEL 6.0
#define MIN_ROT_VEL     4.0

/* ── State pool ──────────────────────────────────────────── */
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
    s->active = 0;
}

/* ── wobble_destroy ──────────────────────────────────────── */
void wobble_destroy(Client *c) {
    WobbleState *s = state_for(c);
    if (s) memset(s, 0, sizeof(*s));
}

/* ── wobble_map_bounce ───────────────────────────────────── */
void wobble_map_bounce(Client *c) {
    if (!c->floating) return;
    WobbleState *s = state_for(c);
    if (!s) return;

    s->tx = c->x;  s->ty = c->y;
    s->tw = c->w;  s->th = c->h;

    /* Pop in: start 12% smaller, offset upward for "bounce up" feel */
    double shrink = 0.12;
    s->ax = c->x + c->w * shrink * 0.5;
    s->ay = c->y + c->h * shrink * 0.5 + 16;
    s->aw = c->w * (1.0 - shrink);
    s->ah = c->h * (1.0 - shrink);

    s->vx = 0;  s->vy = -4.0;
    s->vw = 0;  s->vh = 0;

    s->squeeze = 0.08;
    s->squeeze_vel = 0;
    s->rotation = 0;
    s->vrot = 0;

    s->bounce_count = 0;
    s->bounce_energy = 1.0;

    s->wave_phase = 0;
    s->wave_amplitude = 0;
    s->wave_speed = 0;

    s->corner_lag_x = 0;
    s->corner_lag_y = 0;
    s->corner_vel_x = 0;
    s->corner_vel_y = 0;

    s->inertia_x = 0;
    s->inertia_y = 0;

    s->active = 1;
}

/* ── wobble_drop_bounce ──────────────────────────────────── */
void wobble_drop_bounce(Client *c, double vel_x, double vel_y) {
    if (!c->floating) return;
    WobbleState *s = state_for(c);
    if (!s) return;

    s->tx = c->x;  s->ty = c->y;
    s->tw = c->w;  s->th = c->h;

    /* Inertia: carry momentum after release */
    s->inertia_x = vel_x * 0.6;
    s->inertia_y = vel_y * 0.6;
    s->inertia_friction = INERTIA_FRICTION;

    /* Direct velocity for spring overshoot */
    s->vx = vel_x * 0.4;
    s->vy = vel_y * 0.4;

    double speed = sqrt(vel_x * vel_x + vel_y * vel_y);

    /* Squash/stretch: fast movement = more deformation */
    if (speed > MIN_SQUEEZE_VEL) {
        double axis = vel_x / (speed + 0.001);
        s->squeeze = -axis * (speed / 100.0) * 0.25;
        s->squeeze = fmin(0.30, fmax(-0.30, s->squeeze));
        s->squeeze_vel = 0;
        s->last_dir_x = vel_x;
        s->last_dir_y = vel_y;
    }

    /* Rotation wobble: fast horizontal = tilt */
    if (speed > MIN_ROT_VEL) {
        s->vrot = (vel_x / (speed + 0.001)) * (speed / 80.0) * 3.0;
        s->vrot = fmin(5.0, fmax(-5.0, s->vrot));
    }

    /* Corner lag: opposite corner drags behind */
    if (speed > 4.0) {
        s->corner_lag_x = -vel_x * 0.15;
        s->corner_lag_y = -vel_y * 0.15;
        s->corner_vel_x = 0;
        s->corner_vel_y = 0;
    }

    /* Wave propagation: ripple through window */
    if (speed > 8.0) {
        s->wave_phase = 0;
        s->wave_amplitude = fmin(6.0, speed / 20.0);
        s->wave_speed = WAVE_SPEED;
    }

    /* Multi-bounce */
    s->bounce_count = 0;
    s->bounce_energy = fmin(1.0, speed / 60.0);

    s->active = 1;
}

/* ── wobble_edge_bounce ──────────────────────────────────── */
void wobble_edge_bounce(Client *c, int screen_w, int screen_h) {
    if (!c->floating) return;
    WobbleState *s = state_for(c);
    if (!s) return;

    int bounced = 0;

    /* Left edge */
    if (s->ax < 0) {
        s->ax = -s->ax * EDGE_PENETRATION;
        s->vx = fabs(s->vx) * 0.5;
        bounced = 1;
    }
    /* Right edge */
    if (s->ax + s->aw > screen_w) {
        double over = s->ax + s->aw - screen_w;
        s->ax -= over * 2 * EDGE_PENETRATION;
        s->vx = -fabs(s->vx) * 0.5;
        bounced = 1;
    }
    /* Top edge */
    if (s->ay < 0) {
        s->ay = -s->ay * EDGE_PENETRATION;
        s->vy = fabs(s->vy) * 0.5;
        bounced = 1;
    }
    /* Bottom edge */
    if (s->ay + s->ah > screen_h) {
        double over = s->ay + s->ah - screen_h;
        s->ay -= over * 2 * EDGE_PENETRATION;
        s->vy = -fabs(s->vy) * 0.5;
        bounced = 1;
    }

    if (bounced) {
        /* Trigger squeeze on bounce for "squish" feel */
        double speed = sqrt(s->vx * s->vx + s->vy * s->vy);
        s->squeeze = (speed / 80.0) * 0.15;
        s->squeeze = fmin(0.20, fmax(-0.20, s->squeeze));
        s->active = 1;
    }
}

/* ── Apply animated geometry ─────────────────────────────── */
static void apply_geometry(Client *c, WobbleState *s) {
    /* Corner lag deformation: shift origin by lag offset */
    double lag_x = s->corner_lag_x;
    double lag_y = s->corner_lag_y;

    /* Wave displacement: sinusoidal offset along window body */
    double wave = 0;
    if (s->wave_amplitude > 0.1) {
        wave = sin(s->wave_phase) * s->wave_amplitude;
    }

    /* Squash/stretch */
    double sw = s->aw * (1.0 + s->squeeze * 0.5);
    double sh = s->ah * (1.0 - s->squeeze * 0.3);

    /* Apply rotation as scale modulation (can't rotate X windows) */
    double rot_scale = 1.0 + fabs(s->rotation) * 0.003;
    sw *= rot_scale;
    sh /= rot_scale;

    /* Clamp */
    if (sw < 80)  sw = 80;
    if (sh < 40)  sh = 40;

    int nx = (int)(s->ax + lag_x + wave);
    int ny = (int)(s->ay + lag_y);
    int nw = (int)sw;
    int nh = (int)sh;

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

/* ── Spring step ─────────────────────────────────────────── */
static void spring_step(double *pos, double *vel,
                        double target, double k, double d) {
    double a = k * (target - *pos) - d * (*vel);
    *vel += a;
    *pos += *vel;
}

/* ── wobble_step_all ─────────────────────────────────────── */
int wobble_step_all(void) {
    int active = 0;

    /* Get screen dimensions for edge bounce */
    int screen_w = DisplayWidth(wm->dpy, DefaultScreen(wm->dpy));
    int screen_h = DisplayHeight(wm->dpy, DefaultScreen(wm->dpy));

    for (int i = 0; i < wm->num_clients; i++) {
        Client *c = wm->all_clients[i];
        if (!c) continue;
        WobbleState *s = &s_states[i];
        if (!s->active) continue;

        /* Snap tiled windows immediately */
        if (!c->floating) {
            s->ax = c->x; s->ay = c->y;
            s->aw = c->w; s->ah = c->h;
            s->vx = s->vy = s->vw = s->vh = 0;
            s->squeeze = s->squeeze_vel = 0;
            s->rotation = s->vrot = 0;
            s->inertia_x = s->inertia_y = 0;
            s->corner_lag_x = s->corner_lag_y = 0;
            s->active = 0;
            continue;
        }

        /* Apply inertia (momentum after release) */
        if (fabs(s->inertia_x) > INERTIA_MIN ||
            fabs(s->inertia_y) > INERTIA_MIN) {
            s->tx += s->inertia_x;
            s->ty += s->inertia_y;
            s->inertia_x *= s->inertia_friction;
            s->inertia_y *= s->inertia_friction;
            /* Apply gravity during inertia */
            s->inertia_y += GRAVITY;
        } else {
            s->inertia_x = 0;
            s->inertia_y = 0;
            /* Gravity on idle floating windows */
            s->ty += GRAVITY * 0.3;
        }

        /* Edge bounce */
        wobble_edge_bounce(c, screen_w, screen_h);

        /* Position springs */
        spring_step(&s->ax, &s->vx, s->tx, K_POS, D_POS);
        spring_step(&s->ay, &s->vy, s->ty, K_POS, D_POS);

        /* Size springs */
        spring_step(&s->aw, &s->vw, s->tw, K_SIZE, D_SIZE);
        spring_step(&s->ah, &s->vh, s->th, K_SIZE, D_SIZE);

        /* Rotation spring */
        spring_step(&s->rotation, &s->vrot, 0.0, K_ROT, D_ROT);

        /* Squash spring */
        spring_step(&s->squeeze, &s->squeeze_vel, 0.0, K_SQUEEZE, D_SQUEEZE);

        /* Corner lag spring */
        spring_step(&s->corner_lag_x, &s->corner_vel_x, 0.0, K_CORNER, D_CORNER);
        spring_step(&s->corner_lag_y, &s->corner_vel_y, 0.0, K_CORNER, D_CORNER);

        /* Wave propagation */
        if (s->wave_amplitude > 0.1) {
            s->wave_phase += s->wave_speed;
            s->wave_amplitude *= WAVE_DECAY;
        } else {
            s->wave_amplitude = 0;
        }

        apply_geometry(c, s);

        /* Check if settled */
        double total_vel = fabs(s->vx) + fabs(s->vy) +
                          fabs(s->vw) + fabs(s->vh) +
                          fabs(s->vrot) + fabs(s->squeeze_vel);
        double total_pos = fabs(s->ax - s->tx) + fabs(s->ay - s->ty) +
                          fabs(s->aw - s->tw) + fabs(s->ah - s->th);

        if (total_pos < SETTLE_EPS && total_vel < SETTLE_EPS &&
            fabs(s->inertia_x) < INERTIA_MIN &&
            fabs(s->inertia_y) < INERTIA_MIN &&
            fabs(s->squeeze) < 0.005 &&
            fabs(s->rotation) < 0.1 &&
            s->wave_amplitude < 0.1) {

            /* Multi-bounce: if we still have energy, do another bounce */
            if (s->bounce_count < MAX_BOUNCES && s->bounce_energy > 0.05) {
                s->bounce_count++;
                s->bounce_energy *= BOUNCE_DECAY;
                /* Small random perturbation for organic feel */
                s->vx += ((double)(rand() % 100 - 50) / 50.0) * s->bounce_energy * 2.0;
                s->vy += ((double)(rand() % 100 - 50) / 50.0) * s->bounce_energy * 2.0;
                s->squeeze += s->bounce_energy * 0.05;
                active++;
            } else {
                /* Fully settled — snap to exact target */
                s->ax = s->tx; s->ay = s->ty;
                s->aw = s->tw; s->ah = s->th;
                s->squeeze = 0;
                s->rotation = 0;
                s->inertia_x = s->inertia_y = 0;
                s->corner_lag_x = s->corner_lag_y = 0;
                s->wave_amplitude = 0;
                s->active = 0;

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
            }
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
