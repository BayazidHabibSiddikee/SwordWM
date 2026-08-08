/* =========================================================
 * physics.c — real window physics for SwordWM
 *
 * Combines fwm's rigid-body concepts with X11 spring model.
 * Pure software physics — no Box2D dependency.
 * Animates XMoveResizeWindow directly.
 *
 * Integration: semi-implicit Euler (stable, simple)
 * Collision: AABB overlap detection + impulse response
 * Springs: damped harmonic oscillators for wobble/deformation
 * ========================================================= */
#include "swordwm.h"
#include "physics.h"
#include "config_parser.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <float.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Default physics tuning ──────────────────────────────── */
#define DEFAULT_GRAVITY         981.0   /* px/s^2 (earth-like at 100px/m) */
#define DEFAULT_FRICTION        0.985   /* per-step velocity retention */
#define DEFAULT_RESTITUTION     0.3     /* wall bounce (dull, like furniture) */
#define DEFAULT_MASS_DENSITY    0.0005  /* mass per px^2 */
#define DEFAULT_THROW_MULT      0.65    /* throw speed multiplier */
#define DEFAULT_MAX_THROW       1800.0  /* px/s cap */
#define DEFAULT_STOP_THRESH     1.0     /* px/s: below this = stopped */
#define DEFAULT_WALL_THICK      60.0    /* wall thickness for collision */

/* ── Spring wobble tuning (from fwm wobble.c) ───────────── */
#define SPRING_K_HOME   200.0   /* spring stiffness to rest position */
#define SPRING_C        16.0    /* damping rate (0.57 * critical) */
#define SPRING_K_EDGE   (SPRING_K_HOME * 0.3 * 0.3 * (WOBBLE_GRID - 1) * (WOBBLE_GRID - 1))
#define SPRING_C_EDGE   (0.6 * sqrt(SPRING_K_EDGE))
#define SPRING_GRIP     8.0     /* grip multiplier at grab point */
#define SPRING_GRIP_SPAN 0.25   /* grip falloff as fraction of window */

#define WOBBLE_GRID     9
#define WOBBLE_POINTS   (WOBBLE_GRID * WOBBLE_GRID)
#define WOBBLE_STEP_S   (1.0 / 480.0)
#define WOBBLE_MAX_STEPS 128
#define WOBBLE_REST_PX  0.6
#define WOBBLE_REST_VEL 10.0

/* ── Animation spring tuning ─────────────────────────────── */
#define K_POS           0.25
#define D_POS           0.78
#define K_SIZE          0.20
#define D_SIZE          0.74
#define K_ROT           0.15
#define D_ROT           0.70
#define K_SQUEEZE       0.22
#define D_SQUEEZE       0.65
#define K_CORNER        0.12
#define D_CORNER        0.60
#define INERTIA_FRICTION 0.92
#define INERTIA_MIN     0.3
#define EDGE_BOUNCE_K   0.35
#define EDGE_BOUNCE_D   0.70
#define EDGE_PENETRATION 0.4
#define MAX_BOUNCES     4
#define BOUNCE_DECAY    0.55
#define WAVE_SPEED      0.15
#define WAVE_DECAY      0.97
#define GRAVITY_EFFECT  0.12
#define SETTLE_EPS      0.5
#define MIN_SQUEEZE_VEL 6.0
#define MIN_ROT_VEL     4.0

/* ── Collision filtering ─────────────────────────────────── */
#define CAT_WINDOW  0x0001u
#define CAT_WALL    0x0002u

/* ── Forward declarations ────────────────────────────────── */
static void slot_create(PhysicsWorld *world, int i, PhysicsBody *m);
static void slot_release(PhysicsWorld *world, int i);
static double calc_mass(int width, int height, double density);
static int rects_overlap(int ax, int ay, int aw, int ah,
                         int bx, int by, int bw, int bh);
static void resolve_collision(PhysicsBody *a, PhysicsBody *b,
                               double nx, double ny, double speed,
                               double restitution);
static void clamp_velocity(double *vx, double *vy, double max_speed);

/* ── Animation helpers ───────────────────────────────────── */
static void anim_spring_step(double *pos, double *vel,
                              double target, double k, double d);
static void anim_apply_geometry(Client *c, PhysicsAnimState *anim);

/* =========================================================
 * Core physics
 * ========================================================= */

static double calc_mass(int width, int height, double density) {
    return (double)(width * height) * density;
}

static int rects_overlap(int ax, int ay, int aw, int ah,
                         int bx, int by, int bw, int bh) {
    if (ax + aw <= bx) return 0;
    if (bx + bw <= ax) return 0;
    if (ay + ah <= by) return 0;
    if (by + bh <= ay) return 0;
    return 1;
}

static void clamp_velocity(double *vx, double *vy, double max_speed) {
    if (max_speed <= 0.0) return;  /* 0 = no limit */
    double speed = hypot(*vx, *vy);
    if (speed <= max_speed || speed <= 0.0) return;
    double scale = max_speed / speed;
    *vx *= scale;
    *vy *= scale;
}

/* =========================================================
 * Body slot management (parallel to PhysicsWorld.bodies)
 * ========================================================= */

static void slot_release(PhysicsWorld *world, int i) {
    /* No-op for now — no Box2D body to destroy */
    (void)world; (void)i;
}

static void slot_create(PhysicsWorld *world, int i, PhysicsBody *m) {
    /* Initialize body state from workspace profile */
    int d = m->workspace_id;
    if (d < 0 || d >= 9) d = 0;
    PhysicsProfile *p = &world->profiles[d];

    m->mass = calc_mass(m->width, m->height, world->mass_density);
    if (!isnan(m->rule_mass)) m->mass *= m->rule_mass;

    /* Material properties resolved from profile + rules */
    (void)p; /* applied during step */
    (void)i;
}

/* =========================================================
 * Collision detection & response
 * ========================================================= */

/* Compute AABB collision between two bodies.
 * Returns 1 if overlapping, sets normal and overlap depth. */
static int compute_collision(const PhysicsBody *a, const PhysicsBody *b,
                              double *nx, double *ny, double *depth) {
    double acx = a->x + a->width / 2.0;
    double acy = a->y + a->height / 2.0;
    double bcx = b->x + b->width / 2.0;
    double bcy = b->y + b->height / 2.0;

    double dx = bcx - acx;
    double dy = bcy - acy;

    double half_w = (a->width + b->width) / 2.0;
    double half_h = (a->height + b->height) / 2.0;

    double overlap_x = half_w - fabs(dx);
    double overlap_y = half_h - fabs(dy);

    if (overlap_x <= 0 || overlap_y <= 0) return 0;

    /* Minimum overlap axis */
    if (overlap_x < overlap_y) {
        *nx = (dx > 0) ? 1.0 : -1.0;
        *ny = 0.0;
        *depth = overlap_x;
    } else {
        *nx = 0.0;
        *ny = (dy > 0) ? 1.0 : -1.0;
        *depth = overlap_y;
    }
    return 1;
}

/* Impulse-based collision response (from fwm's approach). */
static void resolve_collision(PhysicsBody *a, PhysicsBody *b,
                               double nx, double ny, double speed,
                               double restitution) {
    (void)speed;  /* used for impact recording, not here */
    /* Relative velocity along normal */
    double rvx = b->vx - a->vx;
    double rvy = b->vy - a->vy;
    double rv_dot_n = rvx * nx + rvy * ny;

    /* Only resolve if approaching */
    if (rv_dot_n > 0) return;

    /* Impulse scalar (equal mass assumption for simplicity) */
    double e = restitution;
    double j = -(1.0 + e) * rv_dot_n;
    j /= (1.0 / a->mass + 1.0 / b->mass);

    /* Apply impulse */
    double jnx = j * nx / a->mass;
    double jny = j * ny / a->mass;
    a->vx -= jnx;
    a->vy -= jny;
    b->vx += j * nx / b->mass;
    b->vy += j * ny / b->mass;

    /* Separate overlapping bodies */
    double separation = 0.5;
    a->x -= nx * separation;
    a->y -= ny * separation;
    b->x += nx * separation;
    b->y += ny * separation;

    a->flying = 1;
    b->flying = 1;
}

/* Wall collision with restitution (from fwm). */
static void wall_bounce(PhysicsBody *m, int screen_w, int screen_h,
                         double restitution) {
    int bounced = 0;

    /* Left wall */
    if (m->x < 0) {
        m->x = 0;
        m->vx = fabs(m->vx) * restitution;
        bounced = 1;
    }
    /* Right wall */
    if (m->x + m->width > screen_w) {
        m->x = screen_w - m->width;
        m->vx = -fabs(m->vx) * restitution;
        bounced = 1;
    }
    /* Top wall */
    if (m->y < 0) {
        m->y = 0;
        m->vy = fabs(m->vy) * restitution;
        bounced = 1;
    }
    /* Bottom wall */
    if (m->y + m->height > screen_h) {
        m->y = screen_h - m->height;
        m->vy = -fabs(m->vy) * restitution;
        bounced = 1;
    }

    if (bounced) {
        m->flying = 1;
        m->impact_timer = 0.0;
    }
}

/* =========================================================
 * Physics world lifecycle
 * ========================================================= */

void physics_init(PhysicsWorld *world) {
    memset(world, 0, sizeof(*world));

    world->gravity          = DEFAULT_GRAVITY;
    world->gravity_scale    = 1.0;
    world->friction         = DEFAULT_FRICTION;
    world->mass_density     = DEFAULT_MASS_DENSITY;
    world->restitution      = DEFAULT_RESTITUTION;
    world->throw_speed_mult = DEFAULT_THROW_MULT;
    world->max_throw_speed  = DEFAULT_MAX_THROW;
    world->stop_threshold   = DEFAULT_STOP_THRESH;

    physics_reset_profiles(world);
}

void physics_reset_profiles(PhysicsWorld *world) {
    for (int i = 0; i < 9; i++) {
        world->profiles[i].gravity      = world->gravity;
        world->profiles[i].friction     = world->friction;
        world->profiles[i].restitution  = world->restitution;
        world->profiles[i].mass_density = world->mass_density;
    }
}

void physics_destroy(PhysicsWorld *world) {
    memset(world, 0, sizeof(*world));
}

/* =========================================================
 * Body management
 * ========================================================= */

PhysicsBody *physics_sync_body(PhysicsWorld *world, uint32_t id,
                                int x, int y, int width, int height,
                                int screen_width) {
    /* Find existing body */
    for (int i = 0; i < world->body_count; i++) {
        if (world->bodies[i].active && world->bodies[i].id == id) {
            PhysicsBody *m = &world->bodies[i];
            m->x = x; m->y = y;
            m->width = width; m->height = height;
            m->mass = calc_mass(width, height, world->mass_density);
            if (!isnan(m->rule_mass)) m->mass *= m->rule_mass;
            int d = (int)((m->x + m->width / 2.0) / screen_width);
            if (d < 0) d = 0;
            if (d >= 9) d = 8;
            m->workspace_id = d;
            return m;
        }
    }

    /* Reuse inactive slot */
    PhysicsBody *body = NULL;
    for (int i = 0; i < world->body_count; i++) {
        if (!world->bodies[i].active) {
            slot_release(world, i);
            body = &world->bodies[i];
            break;
        }
    }

    if (!body) {
        if (world->body_count >= PHYSICS_MAX_WINDOWS) return NULL;
        body = &world->bodies[world->body_count++];
    }

    memset(body, 0, sizeof(*body));
    body->id = id;
    body->active = 1;
    body->x = x; body->y = y;
    body->width = width; body->height = height;
    body->rule_mass = body->rule_gravity = NAN;
    body->rule_bounce = body->rule_friction = NAN;
    body->mass = calc_mass(width, height, world->mass_density);

    int d = (int)((body->x + body->width / 2.0) / screen_width);
    if (d < 0) d = 0;
    if (d >= 9) d = 8;
    body->workspace_id = d;

    slot_create(world, world->body_count - 1, body);
    return body;
}

void physics_stop_body(PhysicsWorld *world, uint32_t id) {
    for (int i = 0; i < world->body_count; i++) {
        PhysicsBody *m = &world->bodies[i];
        if (m->active && m->id == id) {
            m->flying = 0;
            m->vx = 0; m->vy = 0;
            m->angvel = 0;
            return;
        }
    }
}

void physics_throw_body(PhysicsWorld *world, uint32_t id, double vx, double vy) {
    for (int i = 0; i < world->body_count; i++) {
        PhysicsBody *m = &world->bodies[i];
        if (m->active && m->id == id) {
            m->flying = 1;
            m->vx = vx * world->throw_speed_mult;
            m->vy = vy * world->throw_speed_mult;
            clamp_velocity(&m->vx, &m->vy, world->max_throw_speed);
            return;
        }
    }
}

void physics_set_velocity(PhysicsWorld *world, uint32_t id, double vx, double vy) {
    for (int i = 0; i < world->body_count; i++) {
        if (world->bodies[i].active && world->bodies[i].id == id) {
            world->bodies[i].vx = vx;
            world->bodies[i].vy = vy;
            return;
        }
    }
}

void physics_remove_body(PhysicsWorld *world, uint32_t id) {
    for (int i = 0; i < world->body_count; i++) {
        if (world->bodies[i].id == id) {
            world->bodies[i].active = 0;
            world->bodies[i].id = 0;
            slot_release(world, i);
            return;
        }
    }
}

PhysicsBody *physics_find_body(PhysicsWorld *world, uint32_t id) {
    for (int i = 0; i < world->body_count; i++) {
        if (world->bodies[i].active && world->bodies[i].id == id)
            return &world->bodies[i];
    }
    return NULL;
}

void physics_spin_body(PhysicsWorld *world, uint32_t id, double angvel) {
    PhysicsBody *m = physics_find_body(world, id);
    if (!m) return;
    m->spin = 1;
    m->angvel = angvel;
}

void physics_unspin_body(PhysicsWorld *world, uint32_t id) {
    PhysicsBody *m = physics_find_body(world, id);
    if (!m) return;
    m->spin = 0;
    m->angle = 0;
    m->angvel = 0;
}

void physics_push_away(PhysicsWorld *world, uint32_t pushed, uint32_t pusher, double speed) {
    PhysicsBody *a = physics_find_body(world, pusher);
    PhysicsBody *b = physics_find_body(world, pushed);
    if (!a || !b) return;

    double ax = a->x + a->width / 2.0;
    double ay = a->y + a->height / 2.0;
    double bx = b->x + b->width / 2.0;
    double by = b->y + b->height / 2.0;

    double dx = bx - ax;
    double dy = by - ay;
    double len = hypot(dx, dy);
    if (len < 1.0) { dx = 1.0; dy = 0.0; len = 1.0; }

    b->vx = (dx / len) * speed;
    b->vy = (dy / len) * speed;
    b->flying = 1;
}

void physics_push_overlapping(PhysicsWorld *world, uint32_t pusher, double speed) {
    PhysicsBody *a = physics_find_body(world, pusher);
    if (!a) return;

    for (int i = 0; i < world->body_count; i++) {
        PhysicsBody *b = &world->bodies[i];
        if (!b->active || b->id == pusher || b->no_collide || b->fullscreen || b->tiled)
            continue;
        if (b->workspace_id != a->workspace_id) continue;

        if (!rects_overlap((int)a->x, (int)a->y, a->width, a->height,
                           (int)b->x, (int)b->y, b->width, b->height))
            continue;

        double dx = (b->x + b->width / 2.0) - (a->x + a->width / 2.0);
        double dy = (b->y + b->height / 2.0) - (a->y + a->height / 2.0);
        double len = hypot(dx, dy);
        if (len < 1.0) { dx = 1.0; dy = 0.0; len = 1.0; }

        b->vx = (dx / len) * speed;
        b->vy = (dy / len) * speed;
        b->flying = 1;
    }
}

/* =========================================================
 * Hit points (from fwm)
 * ========================================================= */

double physics_body_hp(const PhysicsBody *b) {
    if (!b) return 0.0;
    double base = b->mass;
    double tough = isnan(b->rule_mass) ? 1.0 : b->rule_mass;
    double hp = base * tough;
    return hp > 0.0 ? hp : 0.0;
}

double physics_body_hardness(const PhysicsBody *b) {
    if (!b) return 0.0;
    return isnan(b->rule_mass) ? 1.0 : b->rule_mass;
}

/* =========================================================
 * Physics step — the core simulation loop
 * ========================================================= */

void physics_step(PhysicsWorld *world, int screen_width, int screen_height,
                  uint32_t dragged_id, double dt) {
    if (dt <= 0.0 || dt > 0.5) dt = 1.0 / 60.0;

    world->impact_count = 0;

    /* ── Sub-stepping for stability (from fwm) ────────────── */
    int subs = 1;
    double max_advance = 32.0;  /* max px per substep */
    for (int i = 0; i < world->body_count; i++) {
        PhysicsBody *m = &world->bodies[i];
        if (!m->active || !m->flying) continue;
        double advance = hypot(m->vx, m->vy) * dt;
        if (advance > max_advance) {
            int need = (int)ceil(advance / max_advance);
            if (need > subs) subs = need;
        }
    }
    if (subs > 8) subs = 8;
    double sdt = dt / subs;

    for (int step = 0; step < subs; step++) {
        /* ── Apply gravity (from fwm: per-body gravity_scale) ── */
        double g = world->gravity * world->gravity_scale;
        for (int i = 0; i < world->body_count; i++) {
            PhysicsBody *m = &world->bodies[i];
            if (!m->active || m->pinned || m->fullscreen || m->tiled || m->floating)
                continue;
            if (m->id == dragged_id) continue;

            /* Per-body gravity scale */
            double gscale = (world->gravity != 0.0)
                ? world->profiles[m->workspace_id].gravity / world->gravity
                : 0.0;
            if (!isnan(m->rule_gravity)) gscale *= m->rule_gravity;

            m->vy += g * gscale * sdt;
        }

        /* ── Integrate positions (semi-implicit Euler) ──────── */
        for (int i = 0; i < world->body_count; i++) {
            PhysicsBody *m = &world->bodies[i];
            if (!m->active || m->pinned || m->fullscreen || m->tiled || m->floating)
                continue;
            if (m->id == dragged_id) continue;

            /* Apply friction (per-step retention) */
            double friction = world->profiles[m->workspace_id].friction;
            if (!isnan(m->rule_friction)) friction = m->rule_friction;
            m->vx *= friction;
            m->vy *= friction;

            /* Angular damping */
            if (m->spin) {
                m->angvel *= 0.995;
            }

            /* Integrate */
            m->x += m->vx * sdt;
            m->y += m->vy * sdt;
            m->angle += m->angvel * sdt;

            /* Speed check */
            double speed = hypot(m->vx, m->vy);
            m->flying = (speed > world->stop_threshold) ? 1 : 0;
            if (!m->flying) {
                m->vx = 0;
                m->vy = 0;
            }
        }

        /* ── Wall collisions (from fwm) ────────────────────── */
        for (int i = 0; i < world->body_count; i++) {
            PhysicsBody *m = &world->bodies[i];
            if (!m->active || m->pinned || m->fullscreen || m->tiled || m->floating)
                continue;
            if (m->id == dragged_id) continue;

            double rest = world->profiles[m->workspace_id].restitution;
            if (!isnan(m->rule_bounce)) rest = m->rule_bounce;
            wall_bounce(m, screen_width, screen_height, rest);
        }

        /* ── Window-to-window collisions ───────────────────── */
        for (int i = 0; i < world->body_count; i++) {
            PhysicsBody *a = &world->bodies[i];
            if (!a->active || a->no_collide || a->pinned || a->floating)
                continue;

            for (int j = i + 1; j < world->body_count; j++) {
                PhysicsBody *b = &world->bodies[j];
                if (!b->active || b->no_collide || b->pinned || b->floating)
                    continue;
                if (a->workspace_id != b->workspace_id) continue;

                double nx, ny, depth;
                if (!compute_collision(a, b, &nx, &ny, &depth))
                    continue;

                /* Skip if either is the dragged window */
                if (a->id == dragged_id || b->id == dragged_id) continue;

                /* Compute approach speed */
                double rvx = b->vx - a->vx;
                double rvy = b->vy - a->vy;
                double speed = fabs(rvx * nx + rvy * ny);

                /* Restitution from workspace profile */
                double rest = world->profiles[a->workspace_id].restitution;

                resolve_collision(a, b, nx, ny, speed, rest);

                /* Record impact */
                if (world->impact_count < PHYSICS_MAX_IMPACTS) {
                    PhysicsImpact *im = &world->impacts[world->impact_count++];
                    im->id_a = a->id;
                    im->id_b = b->id;
                    im->x = (a->x + a->width / 2.0 + b->x + b->width / 2.0) / 2.0;
                    im->y = (a->y + a->height / 2.0 + b->y + b->height / 2.0) / 2.0;
                    im->nx = nx;
                    im->ny = ny;
                    im->speed = speed;
                }
            }
        }

        /* ── Speed clamping (from fwm) ─────────────────────── */
        for (int i = 0; i < world->body_count; i++) {
            PhysicsBody *m = &world->bodies[i];
            if (!m->active) continue;
            clamp_velocity(&m->vx, &m->vy, world->max_throw_speed);
        }
    }

    /* ── Update impact timers ──────────────────────────────── */
    for (int i = 0; i < world->body_count; i++) {
        if (world->bodies[i].active)
            world->bodies[i].impact_timer += dt;
    }
}

/* =========================================================
 * Animation (spring wobble) system
 * ========================================================= */

/* Find animation state index for a client */
static int anim_index_for(Client *c) {
    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->all_clients[i] == c) return i;
    }
    return -1;
}

/* Get animation state for a client from the physics world */
static PhysicsAnimState *anim_for_client(PhysicsWorld *world, Client *c) {
    if (!world || !c) return NULL;
    int idx = anim_index_for(c);
    if (idx < 0 || idx >= PHYSICS_MAX_WINDOWS) return NULL;
    return &world->anim[idx];
}

static void anim_spring_step(double *pos, double *vel,
                              double target, double k, double d) {
    double a = k * (target - *pos) - d * (*vel);
    *vel += a;
    *pos += *vel;
}

void physics_anim_init(PhysicsWorld *world, Client *c) {
    PhysicsAnimState *anim = anim_for_client(world, c);
    if (!anim) return;
    memset(anim, 0, sizeof(*anim));
    anim->ax = anim->tx = c->x;
    anim->ay = anim->ty = c->y;
    anim->aw = anim->tw = c->w;
    anim->ah = anim->th = c->h;
    anim->active = 0;
}

void physics_anim_map_bounce(PhysicsWorld *world, Client *c) {
    PhysicsAnimState *anim = anim_for_client(world, c);
    if (!anim) return;
    anim->tx = c->x;  anim->ty = c->y;
    anim->tw = c->w;  anim->th = c->h;

    /* Pop in: start 12% smaller, offset upward */
    double shrink = 0.12;
    anim->ax = c->x + c->w * shrink * 0.5;
    anim->ay = c->y + c->h * shrink * 0.5 + 16;
    anim->aw = c->w * (1.0 - shrink);
    anim->ah = c->h * (1.0 - shrink);

    anim->vx = 0;  anim->vy = -4.0;
    anim->vw = 0;  anim->vh = 0;
    anim->squeeze = 0.08;
    anim->squeeze_vel = 0;
    anim->rotation = 0;
    anim->vrot = 0;
    anim->bounce_count = 0;
    anim->bounce_energy = 1.0;
    anim->wave_phase = 0;
    anim->wave_amplitude = 0;
    anim->wave_speed = 0;
    anim->corner_lag_x = 0;
    anim->corner_lag_y = 0;
    anim->corner_vel_x = 0;
    anim->corner_vel_y = 0;
    anim->inertia_x = 0;
    anim->inertia_y = 0;
    anim->active = 1;
}

void physics_anim_drop_bounce(PhysicsWorld *world, Client *c,
                               double vel_x, double vel_y) {
    PhysicsAnimState *anim = anim_for_client(world, c);
    if (!anim) return;
    anim->tx = c->x;  anim->ty = c->y;
    anim->tw = c->w;  anim->th = c->h;

    /* Inertia */
    anim->inertia_x = vel_x * 0.6;
    anim->inertia_y = vel_y * 0.6;
    anim->inertia_friction = INERTIA_FRICTION;

    /* Spring overshoot */
    anim->vx = vel_x * 0.4;
    anim->vy = vel_y * 0.4;

    double speed = sqrt(vel_x * vel_x + vel_y * vel_y);

    /* Squash/stretch */
    if (speed > MIN_SQUEEZE_VEL) {
        double axis = vel_x / (speed + 0.001);
        anim->squeeze = -axis * (speed / 100.0) * 0.25;
        anim->squeeze = fmin(0.30, fmax(-0.30, anim->squeeze));
        anim->squeeze_vel = 0;
        anim->last_dir_x = vel_x;
        anim->last_dir_y = vel_y;
    }

    /* Rotation wobble */
    if (speed > MIN_ROT_VEL) {
        anim->vrot = (vel_x / (speed + 0.001)) * (speed / 80.0) * 3.0;
        anim->vrot = fmin(5.0, fmax(-5.0, anim->vrot));
    }

    /* Corner lag */
    if (speed > 4.0) {
        anim->corner_lag_x = -vel_x * 0.15;
        anim->corner_lag_y = -vel_y * 0.15;
        anim->corner_vel_x = 0;
        anim->corner_vel_y = 0;
    }

    /* Wave propagation */
    if (speed > 8.0) {
        anim->wave_phase = 0;
        anim->wave_amplitude = fmin(6.0, speed / 20.0);
        anim->wave_speed = WAVE_SPEED;
    }

    /* Multi-bounce */
    anim->bounce_count = 0;
    anim->bounce_energy = fmin(1.0, speed / 60.0);
    anim->active = 1;
}

void physics_anim_edge_bounce(PhysicsWorld *world, Client *c,
                               int screen_w, int screen_h) {
    PhysicsAnimState *anim = anim_for_client(world, c);
    if (!anim) return;
    int bounced = 0;

    if (anim->ax < 0) {
        anim->ax = -anim->ax * EDGE_PENETRATION;
        anim->vx = fabs(anim->vx) * 0.5;
        bounced = 1;
    }
    if (anim->ax + anim->aw > screen_w) {
        double over = anim->ax + anim->aw - screen_w;
        anim->ax -= over * 2 * EDGE_PENETRATION;
        anim->vx = -fabs(anim->vx) * 0.5;
        bounced = 1;
    }
    if (anim->ay < 0) {
        anim->ay = -anim->ay * EDGE_PENETRATION;
        anim->vy = fabs(anim->vy) * 0.5;
        bounced = 1;
    }
    if (anim->ay + anim->ah > screen_h) {
        double over = anim->ay + anim->ah - screen_h;
        anim->ay -= over * 2 * EDGE_PENETRATION;
        anim->vy = -fabs(anim->vy) * 0.5;
        bounced = 1;
    }

    if (bounced) {
        double speed = sqrt(anim->vx * anim->vx + anim->vy * anim->vy);
        anim->squeeze = (speed / 80.0) * 0.15;
        anim->squeeze = fmin(0.20, fmax(-0.20, anim->squeeze));
        anim->active = 1;
    }
}

static void anim_apply_geometry(Client *c, PhysicsAnimState *anim) {
    double lag_x = anim->corner_lag_x;
    double lag_y = anim->corner_lag_y;

    double wave = 0;
    if (anim->wave_amplitude > 0.1) {
        wave = sin(anim->wave_phase) * anim->wave_amplitude;
    }

    double sw = anim->aw * (1.0 + anim->squeeze * 0.5);
    double sh = anim->ah * (1.0 - anim->squeeze * 0.3);

    /* Rotation as scale modulation (can't rotate X windows) */
    double rot_scale = 1.0 + fabs(anim->rotation) * 0.003;
    sw *= rot_scale;
    sh /= rot_scale;

    if (sw < 80)  sw = 80;
    if (sh < 40)  sh = 40;

    int nx = (int)(anim->ax + lag_x + wave);
    int ny = (int)(anim->ay + lag_y);
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

int physics_anim_step_all(PhysicsWorld *world) {
    if (!world) world = wm->physics_world;
    if (!world) return 0;
    int active = 0;

    int screen_w = DisplayWidth(wm->dpy, DefaultScreen(wm->dpy));
    int screen_h = DisplayHeight(wm->dpy, DefaultScreen(wm->dpy));

    for (int i = 0; i < wm->num_clients; i++) {
        Client *c = wm->all_clients[i];
        if (!c) continue;
        PhysicsAnimState *anim = &world->anim[i];
        if (!anim->active) continue;

        /* Snap tiled windows immediately */
        if (!c->floating) {
            anim->ax = c->x; anim->ay = c->y;
            anim->aw = c->w; anim->ah = c->h;
            anim->vx = anim->vy = anim->vw = anim->vh = 0;
            anim->squeeze = anim->squeeze_vel = 0;
            anim->rotation = anim->vrot = 0;
            anim->inertia_x = anim->inertia_y = 0;
            anim->corner_lag_x = anim->corner_lag_y = 0;
            anim->active = 0;
            continue;
        }

        /* Apply inertia */
        if (fabs(anim->inertia_x) > INERTIA_MIN ||
            fabs(anim->inertia_y) > INERTIA_MIN) {
            anim->tx += anim->inertia_x;
            anim->ty += anim->inertia_y;
            anim->inertia_x *= anim->inertia_friction;
            anim->inertia_y *= anim->inertia_friction;
            anim->inertia_y += GRAVITY_EFFECT;
        } else {
            anim->inertia_x = 0;
            anim->inertia_y = 0;
            anim->ty += GRAVITY_EFFECT * 0.3;
        }

        /* Edge bounce */
        physics_anim_edge_bounce(world, c, screen_w, screen_h);

        /* Position springs */
        anim_spring_step(&anim->ax, &anim->vx, anim->tx, K_POS, D_POS);
        anim_spring_step(&anim->ay, &anim->vy, anim->ty, K_POS, D_POS);

        /* Size springs */
        anim_spring_step(&anim->aw, &anim->vw, anim->tw, K_SIZE, D_SIZE);
        anim_spring_step(&anim->ah, &anim->vh, anim->th, K_SIZE, D_SIZE);

        /* Rotation spring */
        anim_spring_step(&anim->rotation, &anim->vrot, 0.0, K_ROT, D_ROT);

        /* Squash spring */
        anim_spring_step(&anim->squeeze, &anim->squeeze_vel, 0.0, K_SQUEEZE, D_SQUEEZE);

        /* Corner lag spring */
        anim_spring_step(&anim->corner_lag_x, &anim->corner_vel_x, 0.0, K_CORNER, D_CORNER);
        anim_spring_step(&anim->corner_lag_y, &anim->corner_vel_y, 0.0, K_CORNER, D_CORNER);

        /* Wave propagation */
        if (anim->wave_amplitude > 0.1) {
            anim->wave_phase += anim->wave_speed;
            anim->wave_amplitude *= WAVE_DECAY;
        } else {
            anim->wave_amplitude = 0;
        }

        anim_apply_geometry(c, anim);

        /* Check if settled */
        double total_vel = fabs(anim->vx) + fabs(anim->vy) +
                          fabs(anim->vw) + fabs(anim->vh) +
                          fabs(anim->vrot) + fabs(anim->squeeze_vel);
        double total_pos = fabs(anim->ax - anim->tx) + fabs(anim->ay - anim->ty) +
                          fabs(anim->aw - anim->tw) + fabs(anim->ah - anim->th);

        if (total_pos < SETTLE_EPS && total_vel < SETTLE_EPS &&
            fabs(anim->inertia_x) < INERTIA_MIN &&
            fabs(anim->inertia_y) < INERTIA_MIN &&
            fabs(anim->squeeze) < 0.005 &&
            fabs(anim->rotation) < 0.1 &&
            anim->wave_amplitude < 0.1) {

            /* Multi-bounce */
            if (anim->bounce_count < MAX_BOUNCES && anim->bounce_energy > 0.05) {
                anim->bounce_count++;
                anim->bounce_energy *= BOUNCE_DECAY;
                anim->vx += ((double)(rand() % 100 - 50) / 50.0) * anim->bounce_energy * 2.0;
                anim->vy += ((double)(rand() % 100 - 50) / 50.0) * anim->bounce_energy * 2.0;
                anim->squeeze += anim->bounce_energy * 0.05;
                active++;
            } else {
                /* Fully settled */
                anim->ax = anim->tx; anim->ay = anim->ty;
                anim->aw = anim->tw; anim->ah = anim->th;
                anim->squeeze = 0;
                anim->rotation = 0;
                anim->inertia_x = anim->inertia_y = 0;
                anim->corner_lag_x = anim->corner_lag_y = 0;
                anim->wave_amplitude = 0;
                anim->active = 0;

                c->x = (int)anim->tx; c->y = (int)anim->ty;
                c->w = (int)anim->tw; c->h = (int)anim->th;
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

int physics_anim_any_active(PhysicsWorld *world) {
    if (!world) world = wm->physics_world;
    if (!world) return 0;
    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->all_clients[i] && world->anim[i].active)
            return 1;
    }
    return 0;
}
