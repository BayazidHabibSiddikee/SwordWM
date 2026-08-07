#ifndef WOBBLE_H
#define WOBBLE_H

/* =========================================================
 * wobble.h — advanced spring physics for SwordWM
 *
 * No compositor needed. Animates XMoveResizeWindow directly.
 *
 * Effects (better than Compiz wobbly):
 *   MAP-IN       : pop in from smaller with overshoot bounce
 *   DROP BOUNCE  : release with momentum + squash/stretch
 *   INERTIA     : windows slide after release, decelerate naturally
 *   EDGE BOUNCE : windows bounce off screen edges like rubber
 *   ROTATION    : slight rotation during fast drag (organic feel)
 *   CORNER LAG  : opposite corner lags behind drag (jelly deformation)
 *   WAVE        : movement ripples through window like a wave
 *   MULTI-BOUNCE: multiple diminishing bounces before settling
 *   GRAVITY     : slight downward pull during free-fall
 * ========================================================= */

#include "types.h"

/* ── Per-client spring state ─────────────────────────────── */
typedef struct {
    /* Current animated geometry */
    double ax, ay, aw, ah;
    double rotation;      /* degrees of rotation during drag */

    /* Target geometry */
    double tx, ty, tw, th;

    /* Velocities */
    double vx, vy, vw, vh;
    double vrot;           /* rotation velocity */

    /* Inertia: momentum after release */
    double inertia_x, inertia_y;
    double inertia_friction;

    /* Corner lag (jelly deformation) */
    double corner_lag_x, corner_lag_y;   /* offset of trailing corner */
    double corner_vel_x, corner_vel_y;

    /* Wave propagation */
    double wave_phase;     /* phase of the wave rippling through */
    double wave_amplitude; /* how strong the wave is */
    double wave_speed;     /* how fast the wave travels */

    /* Squash/stretch */
    double squeeze;       /* -1..1: neg=horiz squeeze, pos=vert */
    double squeeze_vel;

    /* Bounce count for multi-bounce settling */
    int bounce_count;
    double bounce_energy;  /* energy remaining in bounce */

    /* 1 = actively animating */
    int active;

    /* Direction of last movement (for squash axis) */
    double last_dir_x, last_dir_y;
} WobbleState;

/* ── API ─────────────────────────────────────────────────── */

void wobble_init(Client *c);
void wobble_destroy(Client *c);
void wobble_map_bounce(Client *c);
void wobble_drop_bounce(Client *c, double vel_x, double vel_y);
void wobble_edge_bounce(Client *c, int screen_w, int screen_h);
int  wobble_step_all(void);
int  wobble_any_active(void);

#endif /* WOBBLE_H */
