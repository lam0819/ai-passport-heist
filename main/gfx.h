// Band drawing: every draw call clips to the band being composed.
//
// Two band buffers take turns: one is composed while the panel's DMA is still
// sending the other, so a frame is never drawn into memory that is in flight.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "assets.h"
#include "config.h"

// Both byte-swapped for the panel.
uint16_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b);
uint16_t gfx_swap(uint16_t natural_rgb565);

// Starts a band at screen row y0 in the next free buffer, filled with bg.
void gfx_band_begin(int y0, int rows, uint16_t bg);
// Same, without the clear, for scenes that cover the whole band anyway.
void gfx_band_begin_raw(int y0, int rows);
const uint16_t *gfx_band_buf(void);
int gfx_band_y0(void);
int gfx_band_rows(void);

void gfx_fill(int x, int y, int w, int h, uint16_t color);
// Mixes color over what is there: alpha 0 (none) .. 256 (solid). For smoke,
// scorch, wet floors and the menu panels; no alpha channel is stored.
// The same mix once, for a solid fill instead of a per-pixel blend.
uint16_t gfx_mix(uint16_t base, uint16_t color, int alpha);
void gfx_blend(int x, int y, int w, int h, uint16_t color, int alpha);
// Skips GFX_TRANS pixels. Silhouette mode paints every opaque pixel `color`.
void gfx_blit(const sprite_t *s, int x, int y);
void gfx_blit_silhouette(const sprite_t *s, int x, int y, uint16_t color);
// Optionally mirrored left-right, optionally as a silhouette.
void gfx_blit_ex(const sprite_t *s, int x, int y, bool flip, bool silhouette, uint16_t color);
void gfx_text(const char *text, int x, int y, int scale, uint16_t color);
int gfx_text_w(const char *text, int scale);
