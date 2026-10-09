// The heist itself: building objects, the thief, guards, pellets and darts.
//
// Everything lives in one fixed-size struct (no allocation); a level is
// parsed from its const map into it by world_load(). The rules follow
// prototype/index.html one for one.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "levels.h"

typedef enum { G_PATROL, G_SLEEP, G_INVESTIGATE, G_ALERT, G_STUNNED } gstate_t;
typedef enum { GT_GUARD, GT_SHIELD, GT_DOG } gtype_t;
typedef enum { BUB_NONE, BUB_Q, BUB_WAKE, BUB_HA } bubble_t;
typedef enum { LS_FLOOR, LS_CARRIED } loot_state_t;

typedef struct {
    float x, vx, home, tx, t, stun, spot, shot_cd, windup, lost_t, near_t, walk_t, wait;
    uint8_t type, f, state, bubble;
    int8_t face;
} guard_t;

typedef struct {
    float x, y, vx, vy;              // y: feet height above the floor (up is +)
    float jhold, run_t, run_v, slip, gun_cd, muzzle, inv, knock, walk_t, push;
    uint8_t f;
    int8_t face;
    bool air, spring, thru, hidden;
    uint8_t ncarry;
    uint8_t carry[MAX_CARRY];        // loot indices, last picked last
} thief_t;

typedef struct { uint8_t col, f; bool open; int8_t side; float anim; } door_t;
typedef struct { uint8_t col, f; int8_t dir; } stairs_t;
typedef struct { uint8_t col, f; } cell_t;
typedef struct { uint8_t col, f; float anim; } spring_t;
typedef struct { float x; uint8_t f, kind, state; uint16_t value; } loot_t;
typedef struct { float x, vx, cd; uint8_t f; } can_t;
typedef struct { float x, cd; uint8_t f; } peel_t;
typedef struct { float x, vx, left; uint8_t f; } shot_t;   // darts and pellets
typedef struct { float x; uint8_t f, w, sp; int8_t dir; } light_t;
typedef struct { char text[10]; float x, y, life; uint16_t color; } fx_t;   // y: px below the top floor's ceiling

typedef enum { EV_NONE, EV_PAUSE, EV_ESCAPE, EV_BONUS_DONE, EV_TIME_UP, EV_BUSTED } wevent_t;

typedef struct {
    const level_t *L;
    int nf, w, y0;                   // y0: screen y of the top floor's ceiling (moves with the
                                     // view when a level is taller than the screen)
    int exit_col;                    // -1: none (bonus)
    uint8_t ndoors, nstairs, nhides, nloot, nguards, ncans, ncrates, npeels, nsprings, nhatches, nlights;
    uint8_t ndarts, nbolts, nfx;
    door_t doors[MAX_DOORS];
    stairs_t stairs[MAX_STAIRS];
    cell_t hides[MAX_HIDES], crates[MAX_CRATES], hatches[MAX_SPRINGS];
    spring_t springs[MAX_SPRINGS];
    loot_t loot[MAX_LOOT];
    guard_t guards[MAX_GUARDS];
    can_t cans[MAX_CANS];
    peel_t peels[MAX_PEELS];
    light_t lights[MAX_LIGHTS];
    shot_t darts[MAX_DARTS], bolts[MAX_BOLTS];
    fx_t fx[MAX_FX];
    thief_t p;
    // OK key timing: tap / hold / jump
    bool ok_active, ok_used;
    float ok_t;
    int score, lives;
    bool ever_spotted, any_alert;
    float time_left, clock, fade;    // fade: short black cut when changing floor
    const char *why;                 // last reason for a hit / end
} world_t;

typedef struct {
    int dir;                         // -1 left, +1 right, 0 none
    bool ok, ok_pressed;             // OK held / went down this step
} winput_t;

void world_load(world_t *w, const level_t *L, int lives);
// Screen y of the top floor's ceiling that shows floor f; taller-than-screen
// levels scroll so the thief's floor and the one above stay in view.
int world_view_y0(const world_t *w, int f);
wevent_t world_update(world_t *w, const winput_t *in, float dt);

// Queries for hints, the HUD and drawing.
static inline float feet_y(const world_t *w, int f) { return (float)(w->y0 + f * FLOOR_H + FEET_DY); }
static inline float thief_cx(const world_t *w) { return w->p.x + BODY_W / 2; }
static inline float level_w(const world_t *w) { return (float)(w->w * TILE); }
int world_loot_left(const world_t *w);
int world_loot_carried(const world_t *w);
bool world_at_exit(const world_t *w);
const stairs_t *world_at_stairs(const world_t *w);
const cell_t *world_at_hide(const world_t *w);
const door_t *world_door_ahead(const world_t *w);
bool world_near_spring(const world_t *w, float range);
bool world_near_crate(const world_t *w, float range);
bool world_near_peel(const world_t *w, float range);
// Exit, bonus end: the result screen's numbers.
typedef struct { int loot, time, ghost, action, stars, total, bars, of; bool bonus, all; } wresult_t;
void world_result(const world_t *w, wresult_t *r);
