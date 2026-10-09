#include "world.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sound.h"

/* ---- small helpers ------------------------------------------------------- */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float signf(float v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f; }
static float gcx(const guard_t *g) { return g->x + BODY_W / 2; }
static float door_x(const door_t *d) { return (float)(d->col * TILE + 8); }

#define FX_GOLD PX(255, 224, 102)
#define FX_RED PX(255, 96, 96)
#define FX_GREEN PX(155, 217, 58)
#define FX_GREY PX(200, 200, 200)
#define FX_ORANGE PX(255, 176, 96)

static void add_fx(world_t *w, const char *text, float x, int f, uint16_t color)
{
    int i = w->nfx < MAX_FX ? w->nfx++ : 0;   /* full: reuse the oldest slot */
    if (w->nfx == MAX_FX) {
        float lo = 9;
        for (int k = 0; k < MAX_FX; k++) {
            if (w->fx[k].life < lo) {
                lo = w->fx[k].life;
                i = k;
            }
        }
    }
    fx_t *e = &w->fx[i];
    strncpy(e->text, text, sizeof(e->text) - 1);
    e->text[sizeof(e->text) - 1] = 0;
    e->x = x;
    e->y = (float)(f * FLOOR_H + FEET_DY - 36);
    e->life = 1.0f;
    e->color = color;
}

static void add_score(world_t *w, int n, float x, int f)
{
    char buf[10];
    w->score += n;
    snprintf(buf, sizeof(buf), "+%d", n);
    add_fx(w, buf, x, f, FX_GOLD);
}

/* ---- load ------------------------------------------------------------------ */

void world_load(world_t *w, const level_t *L, int lives)
{
    memset(w, 0, sizeof(*w));
    w->L = L;
    w->nf = L->floors;
    w->w = L->w;
    w->y0 = world_view_y0(w, L->floors - 1);
    w->exit_col = -1;
    w->lives = lives;
    w->time_left = L->time;
    int li = 0;
    for (int f = 0; f < w->nf; f++) {
        const char *row = L->rows[f];
        for (int col = 0; col < w->w && row[col]; col++) {
            float x = (float)(col * TILE);
            switch (row[col]) {
            case 'S':
                w->p.x = x + 2;
                w->p.f = (uint8_t)f;
                break;
            case 'E': w->exit_col = col; break;
            case 'H': w->hides[w->nhides++] = (cell_t){ (uint8_t)col, (uint8_t)f }; break;
            case '|': w->doors[w->ndoors++] = (door_t){ (uint8_t)col, (uint8_t)f, false, 1, 0 }; break;
            case '^':
            case 'v': w->stairs[w->nstairs++] = (stairs_t){ (uint8_t)col, (uint8_t)f, (int8_t)(row[col] == '^' ? -1 : 1) }; break;
            case 'x': w->crates[w->ncrates++] = (cell_t){ (uint8_t)col, (uint8_t)f }; break;
            case 'o': w->hatches[w->nhatches++] = (cell_t){ (uint8_t)col, (uint8_t)f }; break;
            case 'b': w->springs[w->nsprings++] = (spring_t){ (uint8_t)col, (uint8_t)f, 0 }; break;
            case 'p': w->peels[w->npeels++] = (peel_t){ x + 8, 0, (uint8_t)f }; break;
            case 'c': w->cans[w->ncans++] = (can_t){ x + 6, 0, 0, (uint8_t)f }; break;
            case '$': {
                loot_t *l = &w->loot[w->nloot++];
                l->x = x + 3;
                l->f = (uint8_t)f;
                l->state = LS_FLOOR;
                if (L->bonus) {
                    l->kind = LOOT_BAR;
                    l->value = SCORE_BAR;
                } else {
                    l->kind = L->loot_kind[li];
                    l->value = L->loot_value[li];
                    li++;
                }
                break;
            }
            case 'g':
            case 'G':
            case 'z':
            case 'd': {
                guard_t *g = &w->guards[w->nguards++];
                char ch = row[col];
                g->type = ch == 'G' ? GT_SHIELD : ch == 'd' ? GT_DOG : GT_GUARD;
                g->x = g->home = g->tx = x + 2;
                g->f = (uint8_t)f;
                g->face = ch == 'd' ? -1 : 1;
                g->state = ch == 'z' ? G_SLEEP : G_PATROL;
                break;
            }
            default: break;
            }
        }
    }
    for (int i = 0; i < L->nlights; i++) {
        light_t *s = &w->lights[w->nlights++];
        s->f = L->lights[i][0];
        s->w = L->lights[i][1];
        s->sp = L->lights[i][2];
        s->x = (float)(40 + s->f * 90);
        s->dir = (int8_t)(s->f % 2 ? -1 : 1);
    }
    w->p.face = 1;
    w->p.run_t = 9;
}

int world_view_y0(const world_t *w, int f)
{
    int bottom = SCREEN_H - w->nf * FLOOR_H;             /* ground floor on the bottom edge */
    if (bottom >= HUD_H + 26) {
        return bottom;                                     /* fits: the whole building and the roof */
    }
    int top = SCREEN_H - 3 * FLOOR_H;                     /* the roof and the top three floors */
    int y0 = top - (f - 2 > 0 ? f - 2 : 0) * FLOOR_H;     /* keep floor f on the bottom row */
    return y0 < bottom ? bottom : y0;
}

/* ---- queries ---------------------------------------------------------------- */

int world_loot_left(const world_t *w)
{
    int n = 0;
    for (int i = 0; i < w->nloot; i++) {
        n += w->loot[i].state != LS_CARRIED;
    }
    return n;
}

int world_loot_carried(const world_t *w)
{
    return w->nloot - world_loot_left(w);
}

bool world_at_exit(const world_t *w)
{
    return w->exit_col >= 0 && w->p.f == 0 && fabsf((float)(w->exit_col * TILE + 8) - thief_cx(w)) < 10;
}

const stairs_t *world_at_stairs(const world_t *w)
{
    for (int i = 0; i < w->nstairs; i++) {
        const stairs_t *s = &w->stairs[i];
        if (s->f == w->p.f && fabsf((float)(s->col * TILE + 8) - thief_cx(w)) < 9) {
            return s;
        }
    }
    return NULL;
}

const cell_t *world_at_hide(const world_t *w)
{
    for (int i = 0; i < w->nhides; i++) {
        const cell_t *h = &w->hides[i];
        if (h->f == w->p.f && fabsf((float)(h->col * TILE + 8) - thief_cx(w)) < 9) {
            return h;
        }
    }
    return NULL;
}

const door_t *world_door_ahead(const world_t *w)
{
    for (int i = 0; i < w->ndoors; i++) {
        const door_t *d = &w->doors[i];
        float dx = door_x(d) - thief_cx(w);
        if (d->f == w->p.f && fabsf(dx) < 16 && (d->open || signf(dx) == w->p.face)) {
            return d;
        }
    }
    return NULL;
}

bool world_near_spring(const world_t *w, float range)
{
    for (int i = 0; i < w->nsprings; i++) {
        if (w->springs[i].f == w->p.f && fabsf((float)(w->springs[i].col * TILE + 8) - thief_cx(w)) < range) {
            return true;
        }
    }
    return false;
}

bool world_near_crate(const world_t *w, float range)
{
    for (int i = 0; i < w->ncrates; i++) {
        if (w->crates[i].f == w->p.f && fabsf((float)(w->crates[i].col * TILE + 8) - thief_cx(w)) < range) {
            return true;
        }
    }
    return false;
}

bool world_near_peel(const world_t *w, float range)
{
    for (int i = 0; i < w->npeels; i++) {
        if (w->peels[i].f == w->p.f && fabsf(w->peels[i].x - thief_cx(w)) < range) {
            return true;
        }
    }
    return false;
}

/* Walls: level ends, closed doors and (for anyone below their top) crates. */
static void limits(const world_t *w, int f, float x, float y, float *lo, float *hi)
{
    *lo = 2;
    *hi = level_w(w) - BODY_W - 2;
    float c = x + BODY_W / 2;
    for (int i = 0; i < w->ndoors; i++) {
        const door_t *d = &w->doors[i];
        if (d->f != f || d->open) {
            continue;
        }
        float dx = door_x(d);
        if (dx <= c) {
            *lo = fmaxf(*lo, dx + 3);
        } else {
            *hi = fminf(*hi, dx - 3 - BODY_W);
        }
    }
    if (y < CRATE_H - 1) {
        for (int i = 0; i < w->ncrates; i++) {
            if (w->crates[i].f != f) {
                continue;
            }
            float a = (float)(w->crates[i].col * TILE + 1), b = a + TILE - 2;
            if (b <= c) {
                *lo = fmaxf(*lo, b);
            } else if (a >= c) {
                *hi = fminf(*hi, a - BODY_W);
            }
        }
    }
}

/* What the body at x stands on: a crate top or the floor. */
static float support(const world_t *w, int f, float x)
{
    for (int i = 0; i < w->ncrates; i++) {
        const cell_t *k = &w->crates[i];
        if (k->f == f && x + BODY_W > k->col * TILE + 2 && x < k->col * TILE + TILE - 2) {
            return CRATE_H;
        }
    }
    return 0;
}

static bool closed_door_between(const world_t *w, int f, float a, float b)
{
    float lo = fminf(a, b), hi = fmaxf(a, b);
    for (int i = 0; i < w->ndoors; i++) {
        const door_t *d = &w->doors[i];
        if (d->f == f && !d->open && door_x(d) > lo && door_x(d) < hi) {
            return true;
        }
    }
    return false;
}

static void noise(world_t *w, float x, int f, float r)
{
    for (int i = 0; i < w->nguards; i++) {
        guard_t *g = &w->guards[i];
        if (g->f != f || g->state == G_STUNNED || g->state == G_ALERT) {
            continue;
        }
        float d = fabsf(gcx(g) - x);
        if (d > r || (closed_door_between(w, f, gcx(g), x) && d > 24)) {
            continue;
        }
        g->state = G_INVESTIGATE;
        g->tx = x - BODY_W / 2;
        g->t = 0;
        g->bubble = BUB_Q;
    }
}

static bool can_act(const world_t *w)
{
    return w->p.knock <= 0 && !w->p.air && w->p.slip <= 0;
}

static bool context(const world_t *w)
{
    return world_at_exit(w) || world_at_stairs(w) || world_door_ahead(w);
}

static void stun(world_t *w, guard_t *g, float secs)
{
    g->state = G_STUNNED;
    g->stun = g->type == GT_DOG ? DOG_STUN_S : secs;
    g->spot = 0;
    (void)w;
}

/* ---- player actions -------------------------------------------------------- */

static void hit_player(world_t *w, const char *why)
{
    thief_t *p = &w->p;
    if (p->inv > 0 || p->knock > 0) {
        return;
    }
    sound_play(SFX_HIT);
    w->lives--;
    w->why = why;
    p->knock = KNOCK_S;
    p->vx = 0;
    p->hidden = false;
    p->slip = 0;
    if (p->air) {
        p->y = p->thru ? 0 : support(w, p->f, p->x);
        p->vy = 0;
        p->air = p->spring = p->thru = false;
    }
    add_fx(w, "OUCH!", thief_cx(w), p->f, FX_RED);
    /* Drop only the last thing picked up, just behind. */
    if (p->ncarry > 0) {
        loot_t *l = &w->loot[p->carry[--p->ncarry]];
        l->state = LS_FLOOR;
        l->f = p->f;
        l->x = clampf(p->x - p->face * 14, 4, level_w(w) - 14);
        sound_play(SFX_DROP);
    }
    w->ndarts = 0;
    for (int i = 0; i < w->nguards; i++) {
        guard_t *g = &w->guards[i];
        if (g->state == G_ALERT && g->f == p->f) {
            g->state = G_INVESTIGATE;
            g->tx = g->x;
            g->spot = 0;
            g->t = 0;
            g->bubble = BUB_HA;
        }
    }
}

static void toggle_door(world_t *w, door_t *d, int dir, bool by_player)
{
    sound_play(SFX_DOOR);
    if (d->open) {
        d->open = false;
        return;
    }
    d->open = true;
    d->side = (int8_t)dir;
    d->anim = 0.25f;
    float dx = door_x(d);
    if (by_player) {   /* anyone standing just behind the door gets flattened */
        for (int i = 0; i < w->nguards; i++) {
            guard_t *g = &w->guards[i];
            if (g->f != d->f || g->state == G_STUNNED) {
                continue;
            }
            float off = (gcx(g) - dx) * dir;
            if (off > 0 && off < 22) {
                stun(w, g, SLAM_STUN_S);
                sound_play(SFX_SLAM);
                add_fx(w, "BONK!", gcx(g), g->f, FX_ORANGE);
                add_score(w, SCORE_SLAM, gcx(g), g->f);
                noise(w, dx, d->f, NOISE_SLAM);
            }
        }
    } else if (w->p.f == d->f && !w->p.hidden && w->p.inv <= 0) {
        float off = (thief_cx(w) - dx) * dir;
        if (off > 0 && off < 22) {
            hit_player(w, "FLATTENED BY A DOOR");
        }
    }
}

static void shoot(world_t *w)
{
    thief_t *p = &w->p;
    if (p->gun_cd > 0 || w->nbolts >= MAX_BOLTS) {
        return;
    }
    p->gun_cd = GUN_CD;
    p->muzzle = 0.1f;
    sound_play(SFX_ZAP);
    w->bolts[w->nbolts++] = (shot_t){ thief_cx(w) + p->face * 8, p->face * GUN_V, GUN_RANGE, p->f };
}

static void jump(world_t *w)
{
    thief_t *p = &w->p;
    float run = fmaxf(fabsf(p->vx), p->run_t < RUN_GRACE_S ? p->run_v : 0);
    if (run > fabsf(p->vx)) {
        p->vx = p->face * run;   /* keep the run-up speed through the jump */
    }
    p->vy = JUMP_V;
    p->air = true;
    p->jhold = JUMP_HOLD_S;
    p->hidden = false;
    sound_play(SFX_JUMP);
}

/* A short OK press standing still: exit, stairs, door, else the stun gun. */
static wevent_t tap(world_t *w)
{
    thief_t *p = &w->p;
    if (!can_act(w)) {
        return EV_NONE;
    }
    if (world_at_exit(w)) {
        if (world_loot_left(w) == 0) {
            return EV_ESCAPE;
        }
        sound_play(SFX_LOCKED);
        add_fx(w, "LOCKED", thief_cx(w), p->f, FX_RED);
        return EV_NONE;
    }
    const stairs_t *st = world_at_stairs(w);
    if (st) {
        p->f = (uint8_t)(st->f + st->dir);
        p->vx = 0;
        p->x = (float)(st->col * TILE + 2);
        w->fade = 0.3f;
        sound_play(SFX_STAIRS);
        return EV_NONE;
    }
    door_t *d = (door_t *)world_door_ahead(w);
    if (d) {
        toggle_door(w, d, p->face, true);
        return EV_NONE;
    }
    shoot(w);
    return EV_NONE;
}

/* ---- guards ------------------------------------------------------------------- */

static bool can_see(const world_t *w, const guard_t *g)
{
    const thief_t *p = &w->p;
    if (p->f != g->f || p->hidden || p->knock > 0 || p->spring || p->thru) {
        return false;
    }
    float d = (thief_cx(w) - gcx(g)) * g->face;
    float range = g->type == GT_DOG ? DOG_VISION : VISION;
    if (d < -10 || d > range) {
        return false;
    }
    if (d < 0) {
        return true;   /* bumping into his back */
    }
    return !closed_door_between(w, g->f, gcx(g), thief_cx(w));
}

/* Moves toward tx; true when there (or stuck against a wall). */
static bool move_guard(world_t *w, guard_t *g, float speed, float dt, float tx)
{
    float dir = signf(tx - g->x);
    if (dir == 0 || fabsf(tx - g->x) < 1) {
        g->vx = 0;
        return true;
    }
    g->face = (int8_t)dir;
    g->vx = dir * speed;
    float lo, hi;
    limits(w, g->f, g->x, 0, &lo, &hi);
    float nx = g->x + g->vx * dt;
    if (nx < lo || nx > hi) {
        /* a closed door in the way: alerted / investigating guards open it */
        if (g->state != G_PATROL && g->type != GT_DOG) {
            for (int i = 0; i < w->ndoors; i++) {
                door_t *d = &w->doors[i];
                if (d->f == g->f && !d->open && fabsf(door_x(d) - gcx(g)) < 16 && signf(door_x(d) - gcx(g)) == dir) {
                    toggle_door(w, d, (int)dir, false);
                    return false;
                }
            }
        }
        g->x = clampf(g->x, lo, hi);
        g->vx = 0;
        return true;
    }
    g->x = nx;
    g->walk_t += dt;
    return fabsf(tx - g->x) < 1.5f;
}

static void update_guard(world_t *w, guard_t *g, float dt)
{
    thief_t *p = &w->p;
    g->t += dt;
    if (g->state == G_STUNNED) {
        g->stun -= dt;
        g->vx = 0;
        if (g->stun <= 0) {
            g->state = G_INVESTIGATE;
            g->tx = g->x;
            g->t = 0;
            g->bubble = BUB_Q;
        }
        return;
    }
    if (g->state == G_SLEEP) {
        g->vx = 0;
        bool near = p->f == g->f && !p->hidden && fabsf(thief_cx(w) - gcx(g)) < SLEEP_NEAR;
        g->near_t = near ? g->near_t + dt : fmaxf(0, g->near_t - dt);
        if (g->near_t > SLEEP_WAKE_S) {
            g->state = G_INVESTIGATE;
            g->tx = p->x;
            g->t = 0;
            g->bubble = BUB_WAKE;
        }
        return;
    }
    bool sees = can_see(w, g);
    if (sees) {   /* the spot meter fills; full means alert */
        float dist = fabsf(thief_cx(w) - gcx(g));
        g->spot += dt / SPOT_S * (dist < 40 ? 2 : 1);
        if (g->spot >= 1 && g->state != G_ALERT) {
            g->state = G_ALERT;
            g->spot = 1;
            g->windup = 0;
            g->shot_cd = 0.2f;
            g->lost_t = 0;
            w->ever_spotted = true;
            sound_play(SFX_ALERT);
        }
    } else if (g->state != G_ALERT) {
        g->spot = fmaxf(0, g->spot - dt * (p->hidden ? 1.5f : 0.6f));
    }

    if (g->state == G_ALERT) {
        if (!sees) {
            g->lost_t += dt;
            if (g->lost_t > LOSE_S) {
                g->state = G_INVESTIGATE;
                g->tx = g->x;
                g->t = 0;
                g->spot = 0;
                g->bubble = BUB_Q;
                return;
            }
        } else {
            g->lost_t = 0;
            g->tx = p->x;
        }
        if (g->type == GT_DOG) {
            move_guard(w, g, DOG_CHARGE, dt, g->tx);
        } else {
            float dist = fabsf(thief_cx(w) - gcx(g));
            if (dist > KEEP_DIST || !sees) {
                move_guard(w, g, ALERT_SPEED, dt, g->tx);
            } else {
                g->vx = 0;
                float s = signf(thief_cx(w) - gcx(g));
                if (s != 0) {
                    g->face = (int8_t)s;
                }
            }
            if (sees) {
                g->shot_cd -= dt;
                if (g->shot_cd <= 0) {
                    g->windup += dt;
                    if (g->windup >= SHOT_WINDUP) {
                        g->windup = 0;
                        g->shot_cd = SHOT_CD;
                        if (w->ndarts < MAX_DARTS) {
                            sound_play(SFX_DART);
                            w->darts[w->ndarts++] = (shot_t){ gcx(g) + g->face * 8, g->face * SHOT_V, 2.5f, g->f };
                        }
                    }
                }
            } else {
                g->windup = 0;
            }
        }
    } else if (g->state == G_INVESTIGATE) {
        if (move_guard(w, g, GUARD_SPEED * 1.4f, dt, g->tx)) {
            g->vx = 0;
            if (g->t > 2.5f) {
                g->state = G_PATROL;
                g->t = 0;
                g->bubble = BUB_NONE;
                g->home = g->x;
            } else {
                g->face = ((int)(g->t * 1.5f)) % 2 ? 1 : -1;   /* look around */
            }
        }
    } else if (g->state == G_PATROL) {
        float sp = g->type == GT_DOG ? DOG_SPEED : GUARD_SPEED;
        if (g->wait > 0) {
            g->wait -= dt;
            g->vx = 0;
            if (g->wait <= 0) {
                g->face = (int8_t)-g->face;
            }
        } else {
            bool done = move_guard(w, g, sp, dt, g->home + g->face * (float)(PATROL_COLS * TILE));
            if (done || g->vx == 0) {
                g->wait = PATROL_WAIT;
            }
        }
    }
    /* touching an awake guard (or a dog) catches you, unless you are over him */
    if (p->f == g->f && fabsf(thief_cx(w) - gcx(g)) < 10 && !p->hidden &&
        p->y < (g->type == GT_DOG ? 12 : 30) && g->state != G_STUNNED && g->state != G_SLEEP) {
        if (g->state == G_ALERT || g->type == GT_DOG || can_see(w, g)) {
            hit_player(w, g->type == GT_DOG ? "BITTEN BY THE DOG" : "CAUGHT BY A GUARD");
        }
    }
    if (g->bubble && g->t > 1.5f && g->state != G_ALERT) {
        g->bubble = BUB_NONE;
    }
}

static void bolt_hits(world_t *w, const shot_t *b, guard_t *g)
{
    if (g->type == GT_SHIELD && g->face == -signf(b->vx) && g->state != G_SLEEP) {
        sound_play(SFX_BLOCK);
        add_fx(w, "BLOCKED", gcx(g), g->f, FX_GREY);
        g->state = G_ALERT;
        g->spot = 1;
        g->windup = 0;
        g->shot_cd = 0.2f;
        g->lost_t = 0;
        w->ever_spotted = true;
        return;
    }
    stun(w, g, STUN_S);
    add_score(w, SCORE_STUN, gcx(g), g->f);
}

/* ---- the step ------------------------------------------------------------------ */

static wevent_t update_player(world_t *w, const winput_t *in, float dt)
{
    thief_t *p = &w->p;
    if (p->gun_cd > 0) p->gun_cd -= dt;
    if (p->muzzle > 0) p->muzzle -= dt;
    if (p->inv > 0) p->inv -= dt;
    if (p->knock > 0) {
        p->knock -= dt;
        if (p->knock <= 0) {
            p->inv = INVULN_S;
        }
    }

    /* OK: one key, three jobs. Moving: jump now. Standing: tap = context or
     * stun gun; hold = hide at an alcove, else a jump after TAP_S. */
    wevent_t ev = EV_NONE;
    if (in->ok_pressed) {
        w->ok_active = true;
        w->ok_t = 0;
        w->ok_used = false;
        if (can_act(w) && !context(w) && (fabsf(p->vx) >= MOVE_V || p->run_t < RUN_GRACE_S)) {
            jump(w);
            w->ok_used = true;
        }
    }
    bool held = w->ok_active && in->ok;
    if (held) {
        w->ok_t += dt;
    }
    bool at_h = !p->air && world_at_hide(w);
    if (held && !w->ok_used && w->ok_t > TAP_S && can_act(w) && !at_h) {
        jump(w);
        w->ok_used = true;
    }
    p->hidden = held && !w->ok_used && at_h && p->knock <= 0;
    if (w->ok_active && !in->ok) {
        if (w->ok_t <= TAP_S && !w->ok_used) {
            ev = tap(w);
        }
        w->ok_active = w->ok_used = false;
        p->hidden = false;
    }

    int dir = (!held && p->knock <= 0 && p->slip <= 0) ? in->dir : 0;
    if (dir) {
        p->run_t = 0;
        p->run_v = fabsf(p->vx);
    } else {
        p->run_t += dt;
    }
    if (p->slip > 0) {   /* banana: sliding, no control */
        p->slip -= dt;
        p->vx -= p->vx * fminf(1, 1.9f * dt);
    } else if (dir) {
        p->vx = clampf(p->vx + dir * (p->air ? AIR_ACCEL : ACCEL) * dt, -WALK, WALK);
        p->face = (int8_t)dir;
    } else if (!p->air) {
        float d = FRICTION * dt;
        p->vx = fabsf(p->vx) <= d ? 0 : p->vx - signf(p->vx) * d;
    }
    float lo, hi;
    limits(w, p->f, p->x, p->y, &lo, &hi);
    p->x += p->vx * dt;
    bool pinned = false;
    /* in the air the momentum survives an obstacle, so a late jump still rises onto a crate */
    if (p->x < lo) {
        p->x = lo;
        if (!p->air) p->vx = 0;
        pinned = dir < 0;
    }
    if (p->x > hi) {
        p->x = hi;
        if (!p->air) p->vx = 0;
        pinned = dir > 0;
    }
    p->push = pinned && !p->air ? p->push + dt : 0;
    if (p->push >= PAUSE_PUSH_S) {
        p->push = 0;
        return EV_PAUSE;
    }
    if (p->vx != 0 && !p->air) {
        p->walk_t += dt;
    }

    /* vertical: jumps, crates, springboards */
    float sup = support(w, p->f, p->x);
    if (!p->air && p->y > sup) {   /* walked off a crate */
        p->air = true;
        p->vy = 0;
        p->jhold = 0;
    }
    if (p->air) {
        float g = (p->vy > 0 && p->jhold > 0 && held) ? HOLD_GRAV : GRAVITY;
        p->jhold -= dt;
        p->vy -= g * dt;
        p->y += p->vy * dt;
        if (p->spring && p->y >= FLOOR_H - 8) {   /* through the hatch to the floor above */
            p->f--;
            p->y -= FLOOR_H;
            p->spring = false;
            p->thru = true;
        } else if (!p->spring && p->y > CEIL) {
            p->y = CEIL;
            p->vy = fminf(p->vy, 0);
        }
        if (p->thru && p->y >= 0) {
            p->thru = false;
        }
        if (p->vy <= 0 && p->y <= sup) {
            p->y = sup;
            p->vy = 0;
            p->air = p->spring = p->thru = false;
            sound_play(SFX_LAND);
        }
    }
    if (!p->air && p->y == 0 && p->knock <= 0) {
        for (int i = 0; i < w->nsprings; i++) {
            spring_t *b = &w->springs[i];
            if (b->f == p->f && fabsf((float)(b->col * TILE + 8) - thief_cx(w)) < 6) {
                p->vy = SPRING_V;
                p->vx = 0;
                p->x = (float)(b->col * TILE + 2);
                p->air = p->spring = true;
                p->jhold = 0;
                b->anim = 0.3f;
                sound_play(SFX_BOING);
                add_fx(w, "BOING!", thief_cx(w), p->f, FX_GREEN);
                break;
            }
        }
        for (int i = 0; i < w->npeels && !p->air; i++) {
            peel_t *e = &w->peels[i];
            if (e->cd > 0 || e->f != p->f || fabsf(e->x - thief_cx(w)) > 6 || fabsf(p->vx) < 15) {
                continue;
            }
            e->cd = 1.5f;
            p->slip = SLIP_S;
            p->vx = signf(p->vx) * WALK * 1.3f;
            sound_play(SFX_SLIP);
            add_fx(w, "OOPS!", thief_cx(w), p->f, FX_GOLD);
            noise(w, thief_cx(w), p->f, NOISE_SLIP);
        }
    }
    for (int i = 0; i < w->npeels; i++) {
        if (w->peels[i].cd > 0) w->peels[i].cd -= dt;
    }
    for (int i = 0; i < w->nsprings; i++) {
        if (w->springs[i].anim > 0) w->springs[i].anim -= dt;
    }
    return ev;
}

wevent_t world_update(world_t *w, const winput_t *in, float dt)
{
    thief_t *p = &w->p;
    w->clock += dt;
    if (w->fade > 0) {
        w->fade -= dt;
    }
    int before = (int)w->time_left;
    w->time_left -= dt;
    if (w->time_left < 20 && (int)w->time_left != before) {
        sound_play(SFX_TICK);
    }
    if (w->time_left <= 0) {
        w->time_left = 0;
        if (w->L->bonus) {
            w->why = "TIME UP";
            return EV_BONUS_DONE;
        }
        w->lives--;
        w->why = "TIME UP";
        return w->lives <= 0 ? EV_BUSTED : EV_TIME_UP;
    }

    wevent_t ev = update_player(w, in, dt);
    if (ev == EV_PAUSE || ev == EV_ESCAPE) {
        return ev;
    }

    /* loot: walk over it to grab it */
    for (int i = 0; i < w->nloot; i++) {
        loot_t *l = &w->loot[i];
        if (l->state != LS_FLOOR || l->f != p->f || p->knock > 0 || p->y > 20) {
            continue;
        }
        if (fabsf(l->x + 5 - thief_cx(w)) < 9) {
            l->state = LS_CARRIED;
            p->carry[p->ncarry++] = (uint8_t)i;
            sound_play(SFX_PICK);
            if (w->L->bonus) {
                add_score(w, l->value, thief_cx(w), p->f);
            } else {
                char buf[10];
                snprintf(buf, sizeof(buf), "$%d", l->value);
                add_fx(w, buf, thief_cx(w), p->f, FX_GOLD);
            }
        }
    }
    if (w->L->bonus) {
        if (world_loot_left(w) == 0) {
            w->why = "ALL THE GOLD!";
            return EV_BONUS_DONE;
        }
        for (int i = 0; i < w->nlights; i++) {
            light_t *s = &w->lights[i];
            s->x += s->sp * s->dir * dt;
            if (s->x < 10) {
                s->x = 10;
                s->dir = 1;
            }
            if (s->x > level_w(w) - 10 - s->w) {
                s->x = level_w(w) - 10 - s->w;
                s->dir = -1;
            }
            if (s->f == p->f && !p->hidden && thief_cx(w) > s->x + 2 && thief_cx(w) < s->x + s->w - 2) {
                sound_play(SFX_ALERT);
                w->why = "CAUGHT IN THE LIGHT";
                return EV_BONUS_DONE;
            }
        }
    }
    /* cans: running over one makes a racket */
    for (int i = 0; i < w->ncans; i++) {
        can_t *c = &w->cans[i];
        if (c->cd > 0) c->cd -= dt;
        c->x += c->vx * dt;
        c->vx -= c->vx * fminf(1, 3.9f * dt);
        if (c->f == p->f && !p->air && fabsf(c->x - thief_cx(w)) < 7 && fabsf(p->vx) > 15 && c->cd <= 0) {
            c->cd = 1.0f;
            c->vx = p->vx * 1.6f;
            sound_play(SFX_CLATTER);
            add_fx(w, "CLANG!", c->x, c->f, FX_GREY);
            noise(w, c->x, c->f, NOISE_CAN);
        }
    }

    w->any_alert = false;
    for (int i = 0; i < w->nguards; i++) {
        update_guard(w, &w->guards[i], dt);
        w->any_alert |= w->guards[i].state == G_ALERT;
    }
    for (int i = 0; i < w->ndoors; i++) {
        if (w->doors[i].anim > 0) w->doors[i].anim -= dt;
    }

    /* darts: knee height, a held jump clears them */
    for (int i = 0; i < w->ndarts;) {
        shot_t *s = &w->darts[i];
        s->x += s->vx * dt;
        s->left -= dt;
        for (int k = 0; k < w->ndoors; k++) {
            const door_t *d = &w->doors[k];
            if (d->f == s->f && !d->open && fabsf(door_x(d) - s->x) < 3) s->left = 0;
        }
        if (s->left > 0 && s->f == p->f && !p->hidden && p->y <= DART_H && s->x + 2 > p->x && s->x - 2 < p->x + BODY_W) {
            s->left = 0;
            hit_player(w, "HIT BY A SLEEPY DART");
            if (w->ndarts == 0) break;   /* hit_player clears the darts */
        }
        if (s->left <= 0) {
            w->darts[i] = w->darts[--w->ndarts];
        } else {
            i++;
        }
    }
    /* stun-gun pellets: whoever they touch first sees stars; they pop where they land */
    for (int i = 0; i < w->nbolts;) {
        shot_t *b = &w->bolts[i];
        float stepx = b->vx * dt;
        b->x += stepx;
        b->left -= fabsf(stepx);
        for (int k = 0; k < w->ndoors; k++) {
            const door_t *d = &w->doors[k];
            if (d->f == b->f && !d->open && fabsf(door_x(d) - b->x) < 4) b->left = 0;
        }
        for (int k = 0; k < w->ncrates; k++) {
            const cell_t *c = &w->crates[k];
            if (c->f == b->f && b->x > c->col * TILE && b->x < c->col * TILE + TILE) b->left = 0;
        }
        if (b->left > 0) {
            for (int k = 0; k < w->nguards; k++) {
                guard_t *g = &w->guards[k];
                if (g->f == b->f && g->state != G_STUNNED && fabsf(gcx(g) - b->x) < 8) {
                    b->left = 0;
                    bolt_hits(w, b, g);
                    break;
                }
            }
        }
        if (b->left <= 0) {
            noise(w, b->x, b->f, NOISE_GUN);
            w->bolts[i] = w->bolts[--w->nbolts];
        } else {
            i++;
        }
    }
    for (int i = 0; i < w->nfx;) {
        w->fx[i].life -= dt;
        w->fx[i].y -= 14 * dt;
        if (w->fx[i].life <= 0) {
            w->fx[i] = w->fx[--w->nfx];
        } else {
            i++;
        }
    }
    if (w->lives <= 0) {
        return EV_BUSTED;
    }
    return EV_NONE;
}

void world_result(const world_t *w, wresult_t *r)
{
    memset(r, 0, sizeof(*r));
    if (w->L->bonus) {
        r->bonus = true;
        r->bars = world_loot_carried(w);
        r->of = w->nloot;
        r->all = r->bars == r->of;
        r->total = w->score + (r->all ? SCORE_ALLBARS : 0);
        r->stars = r->all ? 3 : r->bars * 3 >= r->of * 2 ? 2 : 1;   /* all / two thirds / any */
        return;
    }
    for (int i = 0; i < w->p.ncarry; i++) {
        r->loot += w->loot[w->p.carry[i]].value;
    }
    r->time = (int)w->time_left * SCORE_TIME;
    r->ghost = w->ever_spotted ? 0 : SCORE_GHOST;
    r->action = w->score;
    r->stars = 1 + (w->time_left >= w->L->time * 0.4f) + !w->ever_spotted;
    r->total = r->loot + r->time + r->ghost + r->action;
}
