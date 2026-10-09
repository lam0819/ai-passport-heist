// Procedural art: every character and prop is a handful of filled
// rectangles drawn at run time (ported from the prototype's R() calls), so
// there are no sprite bitmaps in flash and nothing in RAM.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "world.h"

// Per-level colours, mixed once at load (no per-pixel alpha in the frame).
void art_level(const world_t *w);
// One band of the building scene: sky, floors, props, people, effects.
// escape_t >= 0 draws the getaway (thief on the balloon) instead of the thief.
void art_world(const world_t *w, int cam, uint32_t frame, float escape_t);
// HUD strip: lives, timer, loot, mini-map, score.
void art_hud(const world_t *w, int cam, int total, uint32_t frame);
// Standalone pieces for the title screen.
void art_thief(int x, int feet, int face, int step, int carry, bool air, bool hidden);
void art_balloon(int x, int y, uint32_t frame);
