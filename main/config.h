// All tunables in one place, one for one with the C object in
// prototype/index.html. tools/gen_levels.py reads the MAX_* limits.
#pragma once

#include <stdint.h>

// --- screen
#define SCREEN_W 320
#define SCREEN_H 240
#define BAND_ROWS 40                 // 6 bands, double-buffered: 2 * 320*40*2 = 50 KB
#define HUD_H 22
#define TILE 16
#define FLOOR_H 64                   // one storey; the feet line is 58 px down
#define FEET_DY 58
#define BODY_W 12                    // thief / guard body width

// --- level limits (fixed arrays, nothing allocated)
#define MAX_COLS 64
#define MAX_FLOORS 4
#define MAX_DOORS 10
#define MAX_HIDES 16
#define MAX_STAIRS 12
#define MAX_LOOT 10
#define MAX_GUARDS 12
#define MAX_CANS 6
#define MAX_CRATES 8
#define MAX_PEELS 6
#define MAX_SPRINGS 4
#define MAX_LIGHTS 4
#define MAX_DARTS 4
#define MAX_BOLTS 3
#define MAX_FX 6
#define MAX_CARRY MAX_LOOT
#define MAX_LEVELS 20               // levels in the table (progress is saved per level)

// --- thief: walking
#define WALK 70.0f                   // px/s
#define ACCEL 420.0f
#define FRICTION 620.0f
#define TAP_S 0.18f                  // OK released sooner than this is a tap
#define PAUSE_PUSH_S 1.5f            // hold a direction into a wall this long: pause menu

// --- thief: jumping (Pip's Passport feel: hold = higher, run-up = farther)
#define JUMP_V 120.0f
#define GRAVITY 520.0f
#define HOLD_GRAV 220.0f             // while rising with OK still held
#define JUMP_HOLD_S 0.25f
#define AIR_ACCEL 260.0f
#define MOVE_V 20.0f                 // faster than this, OK jumps at once
#define RUN_GRACE_S 0.15f            // ...or this soon after letting go of a direction
#define CEIL 21.0f                   // highest feet height inside a storey
#define CRATE_H 14
#define DART_H 12                    // guards' darts fly at knee height
#define SPRING_V 280.0f
#define SLIP_S 0.7f

// --- stun gun (stuns only)
#define GUN_V 220.0f
#define GUN_RANGE 130.0f
#define GUN_CD 0.6f
#define STUN_S 5.0f
#define SLAM_STUN_S 7.0f
#define DOG_STUN_S 3.0f

// --- guards
#define GUARD_SPEED 24.0f
#define ALERT_SPEED 46.0f
#define DOG_SPEED 38.0f
#define DOG_CHARGE 92.0f
#define PATROL_COLS 5
#define PATROL_WAIT 1.2f
#define VISION 100.0f
#define DOG_VISION 84.0f
#define SPOT_S 0.45f
#define LOSE_S 3.0f
#define SHOT_WINDUP 0.5f
#define SHOT_CD 1.5f
#define SHOT_V 120.0f
#define KEEP_DIST 56.0f
#define SLEEP_NEAR 22.0f
#define SLEEP_WAKE_S 1.2f

// --- noise radii (px, same floor)
#define NOISE_CAN 110.0f
#define NOISE_SLIP 110.0f
#define NOISE_GUN 56.0f              // where the pellet pops, not at the muzzle
#define NOISE_SLAM 90.0f

// --- getting caught, timer
#define KNOCK_S 1.1f
#define INVULN_S 2.2f
#define LIVES 3
// timers are per level (level_t.time); 0.4 of it left earns a star

// --- scoring
#define SCORE_STUN 100
#define SCORE_SLAM 300
#define SCORE_TIME 20
#define SCORE_GHOST 3000
#define SCORE_BAR 300
#define SCORE_ALLBARS 5000

// --- states, power
#define INTRO_S 2.6f
#define ESCAPE_S 2.6f
#define DIM_AFTER_S 30.0f            // menus only: backlight down to DIM_PCT
#define DIM_PCT 12
#define SCREEN_OFF_AFTER_S 180.0f    // title only: panel off, any key wakes

// --- colours: byte-swapped RGB565 as compile-time constants
#define RGB565_(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define PX(r, g, b) ((uint16_t)(((RGB565_(r, g, b) >> 8) | (RGB565_(r, g, b) << 8)) & 0xffff))
