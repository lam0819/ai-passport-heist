// Panel bring-up + band transport for the game.
//
// Memory note: there is no full framebuffer here. The game composes the
// screen in horizontal bands (see config.h BAND_ROWS) into two small static
// buffers, so RAM stays in the tens of KB. The panel takes byte-swapped
// RGB565; every pixel from assets or gfx_rgb() is already swapped.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "esp_err.h"

esp_err_t display_init(void);
// Sends one band without waiting for it. It waits for the band before, so
// the buffer given two calls ago is free again when this returns.
void display_send_band(int y, const uint16_t *buf, int rows);
void display_set_backlight(int pct);
// Off: backlight out and the panel asleep. On: both back.
void display_power(bool on);
