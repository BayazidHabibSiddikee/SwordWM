#ifndef WOBBLE_H
#define WOBBLE_H

/* =========================================================
 * wobble.h — anime-style spring physics for window geometry
 *
 * No compositor needed. Animates XMoveResizeWindow directly.
 *
 * Effects:
 *   MAP-IN   : window pops in from slightly smaller + offset,
 *              springs to final size with an overshoot bounce
 *   DROP     : on drag release, final position overshoots and
 *              springs back (watery landing)
 *   SQUEEZE  : while dragging fast, window squishes perpendicular
 *              to movement direction (anime stretch/squash)
 * ========================================================= */

#include "types.h"

/* ── Per-client spring state ─────────────────────────────── */
typedef struct {
    /* Current animated geometry (what we actually draw) */
    double ax, ay, aw, ah;

    /* Target geometry (where the window wants to be) */
    double tx, ty, tw, th;

    /* Velocities */
    double vx, vy, vw, vh;

    /* 1 = this client is actively animating */
    int active;

    /* Squash/stretch axis remembered from last drag direction */
    double squeeze; /* -1..1: neg = horizontal squeeze, pos = vertical */
    double squeeze_vel;
} WobbleState;

/* ── API ─────────────────────────────────────────────────── */

/* Initialize wobble state for a newly managed client.
 * Call from manage_window() after geometry is set. */
void wobble_init(Client *c);

/* Free state for a client being unmanaged. */
void wobble_destroy(Client *c);

/* Trigger map-in bounce (call when window first appears). */
void wobble_map_bounce(Client *c);

/* Trigger drop bounce (call in decorate_button_release).
 * vel_x/vel_y = pointer velocity at release (px/frame). */
void wobble_drop_bounce(Client *c, double vel_x, double vel_y);

/* Step all active animations by one frame.
 * Returns number of still-active animations (0 = all settled). */
int wobble_step_all(void);

/* Returns 1 if any client is still animating. */
int wobble_any_active(void);

#endif /* WOBBLE_H */
