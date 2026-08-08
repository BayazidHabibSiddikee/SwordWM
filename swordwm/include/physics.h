#ifndef PHYSICS_H
#define PHYSICS_H

/* =========================================================
 * physics.h — real window physics for SwordWM
 *
 * Combines fwm's rigid-body concepts with X11 spring model.
 * No compositor needed — animates XMoveResizeWindow directly.
 *
 * Features:
 *   - Verlet integration for stable position-based dynamics
 *   - Gravity with configurable strength per workspace
 *   - AABB window-to-window collision detection & response
 *   - Wall bouncing off screen edges with restitution
 *   - Drag with grip gradient (jelly-like deformation)
 *   - Throwing with momentum and realistic deceleration
 *   - Per-workspace physics profiles (gravity, friction, bounce)
 *   - Window mass derived from area (heavier = harder to push)
 *   - Spin/rotation during drag and free flight
 *   - Squash/stretch on impact and fast movement
 *   - Wave propagation through window body
 *   - Configurable spring damping and stiffness
 * ========================================================= */

#include <stdint.h>
#include "types.h"

/* ── Limits ──────────────────────────────────────────────── */
#define PHYSICS_MAX_WINDOWS     512
#define PHYSICS_MAX_IMPACTS     32
#define PHYSICS_MAX_BARS        128

/* ── Unit system ─────────────────────────────────────────── */
/* Pixels per meter: windows land in ~1..20m range at 100px/m */
#define PX_PER_METER    100.0

/* ── Physics body ────────────────────────────────────────── */
typedef struct {
    uint32_t    id;             /* unique window id */
    int         active;         /* 1 = simulated */

    /* Position (top-left corner, pixels) */
    double      x, y;
    int         width, height;

    /* Velocity (px/s) */
    double      vx, vy;

    /* Mass: area * mass_density. Heavier windows are harder to push. */
    double      mass;

    /* Desktop id for profile lookup */
    int         workspace_id;

    /* Flags */
    int         pinned;         /* immovable anchor */
    int         fullscreen;     /* fullscreen = immovable */
    int         tiled;          /* managed by tiling layout */
    int         floating;       /* floating mode = immovable + no collide */
    int         no_collide;     /* pass through other windows */
    int         flying;         /* 1 = has significant velocity */

    /* Rotation (radians, y-down) */
    int         spin;           /* 1 = free rotation enabled */
    double      angle;          /* current angle */
    double      angvel;         /* angular velocity (rad/s) */

    /* Per-window material overrides (NAN = use workspace profile) */
    double      rule_mass;      /* multiplier on workspace mass_density */
    double      rule_gravity;   /* multiplier on workspace gravity */
    double      rule_bounce;    /* restitution 0..1, absolute */
    double      rule_friction;  /* per-step velocity retention */

    /* Impact stats */
    double      hp;             /* hit points (mass * toughness) */
    double      impact_timer;   /* seconds since last impact */

    /* Saved geometry for unfullscreen/unfloat */
    double      sav_x, sav_y;
    int         sav_w, sav_h;
} PhysicsBody;

/* ── Per-workspace physics profile ──────────────────────── */
typedef struct {
    double      gravity;        /* px/s^2 downward */
    double      friction;       /* per-step velocity retention 0..1 */
    double      restitution;    /* bounciness 0..1 */
    double      mass_density;   /* mass per px^2 */
} PhysicsProfile;

/* ── Collision impact ────────────────────────────────────── */
typedef struct {
    uint32_t    id_a, id_b;     /* window ids; 0 = screen wall */
    double      x, y;           /* impact point (px) */
    double      nx, ny;         /* contact normal A->B */
    double      speed;          /* approach speed (px/s) */
} PhysicsImpact;

/* ── Per-client animation state (spring wobble) ─────────── */
typedef struct {
    /* Current animated geometry */
    double      ax, ay, aw, ah;
    double      rotation;

    /* Target geometry */
    double      tx, ty, tw, th;

    /* Velocities */
    double      vx, vy, vw, vh;
    double      vrot;

    /* Inertia after release */
    double      inertia_x, inertia_y;
    double      inertia_friction;

    /* Corner lag (jelly deformation) */
    double      corner_lag_x, corner_lag_y;
    double      corner_vel_x, corner_vel_y;

    /* Wave propagation */
    double      wave_phase;
    double      wave_amplitude;
    double      wave_speed;

    /* Squash/stretch */
    double      squeeze;
    double      squeeze_vel;

    /* Multi-bounce */
    int         bounce_count;
    double      bounce_energy;

    /* Direction of last movement */
    double      last_dir_x, last_dir_y;

    /* Active flag */
    int         active;
} PhysicsAnimState;

/* ── Physics world ───────────────────────────────────────── */
typedef struct PhysicsWorld {
    PhysicsBody     bodies[PHYSICS_MAX_WINDOWS];
    int             body_count;

    PhysicsImpact   impacts[PHYSICS_MAX_IMPACTS];
    int             impact_count;

    /* Global physics parameters */
    double          gravity;            /* px/s^2 */
    double          gravity_scale;      /* master multiplier (0=off) */
    double          friction;           /* per-step retention */
    double          mass_density;       /* mass per px^2 */
    double          restitution;        /* wall bounce */
    double          throw_speed_mult;   /* throw velocity multiplier */
    double          max_throw_speed;    /* px/s cap */
    double          stop_threshold;     /* velocity below this = stopped */

    /* Per-workspace profiles */
    PhysicsProfile  profiles[9];        /* indexed by workspace id */

    /* Animation states (parallel to wm->all_clients) */
    PhysicsAnimState anim[PHYSICS_MAX_WINDOWS];
} PhysicsWorld;

/* ── API ─────────────────────────────────────────────────── */

/** Initialize the physics world with default parameters. */
void physics_init(PhysicsWorld *world);

/** Reset all workspace profiles to global defaults. */
void physics_reset_profiles(PhysicsWorld *world);

/** Destroy the physics world. */
void physics_destroy(PhysicsWorld *world);

/** Sync a window's physics body. Creates or updates. Returns body pointer. */
PhysicsBody *physics_sync_body(PhysicsWorld *world, uint32_t id,
                                int x, int y, int width, int height,
                                int screen_width);

/** Stop a body's motion instantly. */
void physics_stop_body(PhysicsWorld *world, uint32_t id);

/** Throw a body with velocity (vx, vy) in px/s. */
void physics_throw_body(PhysicsWorld *world, uint32_t id, double vx, double vy);

/** Set a body's velocity directly. */
void physics_set_velocity(PhysicsWorld *world, uint32_t id, double vx, double vy);

/** Remove a body from simulation. */
void physics_remove_body(PhysicsWorld *world, uint32_t id);

/** Find a body by window id. */
PhysicsBody *physics_find_body(PhysicsWorld *world, uint32_t id);

/** Enable/disable free rotation on a body. */
void physics_spin_body(PhysicsWorld *world, uint32_t id, double angvel);
void physics_unspin_body(PhysicsWorld *world, uint32_t id);

/** Push one body away from another at given speed. */
void physics_push_away(PhysicsWorld *world, uint32_t pushed, uint32_t pusher, double speed);

/** Push all overlapping bodies away from a given body. */
void physics_push_overlapping(PhysicsWorld *world, uint32_t pusher, double speed);

/** Advance the simulation by dt seconds. */
void physics_step(PhysicsWorld *world, int screen_width, int screen_height,
                  uint32_t dragged_id, double dt);

/* ── Animation (spring wobble) API ──────────────────────── */

/** Initialize wobble state for a client. */
void physics_anim_init(PhysicsWorld *world, Client *c);

/** Trigger map-in bounce animation. */
void physics_anim_map_bounce(PhysicsWorld *world, Client *c);

/** Trigger drop-bounce with velocity. */
void physics_anim_drop_bounce(PhysicsWorld *world, Client *c,
                               double vel_x, double vel_y);

/** Trigger edge bounce when window hits screen boundary. */
void physics_anim_edge_bounce(PhysicsWorld *world, Client *c,
                               int screen_w, int screen_h);

/** Step all active animations. Returns count of still-active. */
int physics_anim_step_all(PhysicsWorld *world);

/** Check if any animation is still active. */
int physics_anim_any_active(PhysicsWorld *world);

/* ── Hit points ──────────────────────────────────────────── */
double physics_body_hp(const PhysicsBody *b);
double physics_body_hardness(const PhysicsBody *b);

#endif /* PHYSICS_H */
