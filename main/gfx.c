#include "gfx.h"

#include <string.h>

#ifdef GFX_COUNT_PIXELS
long g_gfx_px;   /* host harness: pixels written, for a cost estimate */
#define COUNT(n) (g_gfx_px += (n))
#else
#define COUNT(n) ((void)0)
#endif

static uint16_t s_bufs[2][SCREEN_W * BAND_ROWS];
static uint16_t *s_buf = s_bufs[0];
static int s_next;
static int s_y0, s_rows;

uint16_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return gfx_swap(v);
}

uint16_t gfx_swap(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

void gfx_band_begin(int y0, int rows, uint16_t bg)
{
    /* display_send_band waits for the previous transfer before starting the
     * next, so the buffer two bands back is always free by now. */
    s_buf = s_bufs[s_next];
    s_next ^= 1;
    s_y0 = y0;
    s_rows = rows;
    COUNT(SCREEN_W * rows);
    for (int i = 0; i < SCREEN_W * rows; i++) {
        s_buf[i] = bg;
    }
}

void gfx_band_begin_raw(int y0, int rows)
{
    /* The caller paints every pixel of the band itself: skip the clear. */
    s_buf = s_bufs[s_next];
    s_next ^= 1;
    s_y0 = y0;
    s_rows = rows;
}

const uint16_t *gfx_band_buf(void)
{
    return s_buf;
}

int gfx_band_y0(void)
{
    return s_y0;
}

int gfx_band_rows(void)
{
    return s_rows;
}

void gfx_fill(int x, int y, int w, int h, uint16_t color)
{
    int ry = y - s_y0;
    if (ry < 0) {
        h += ry;
        ry = 0;
    }
    if (ry + h > s_rows) {
        h = s_rows - ry;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (x + w > SCREEN_W) {
        w = SCREEN_W - x;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    COUNT(w * h);
    for (int yy = 0; yy < h; yy++) {
        uint16_t *row = &s_buf[(ry + yy) * SCREEN_W + x];
        for (int xx = 0; xx < w; xx++) {
            row[xx] = color;
        }
    }
}

uint16_t gfx_mix(uint16_t base, uint16_t color, int alpha)
{
    uint16_t v = gfx_swap(base), c = gfx_swap(color);
    int ia = 256 - alpha;
    int r = ((v >> 11) * ia + (c >> 11) * alpha) >> 8;
    int g = (((v >> 5) & 63) * ia + ((c >> 5) & 63) * alpha) >> 8;
    int b = ((v & 31) * ia + (c & 31) * alpha) >> 8;
    return gfx_swap((uint16_t)((r << 11) | (g << 5) | b));
}

void gfx_blend(int x, int y, int w, int h, uint16_t color, int alpha)
{
    if (alpha <= 0) {
        return;
    }
    if (alpha >= 256) {
        gfx_fill(x, y, w, h, color);
        return;
    }
    int ry = y - s_y0;
    if (ry < 0) {
        h += ry;
        ry = 0;
    }
    if (ry + h > s_rows) {
        h = s_rows - ry;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (x + w > SCREEN_W) {
        w = SCREEN_W - x;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    uint16_t c = gfx_swap(color);
    int cr = c >> 11, cg = (c >> 5) & 63, cb = c & 31;
    int ia = 256 - alpha;
    COUNT(w * h * 4);   /* a blend costs about four fills */
    for (int yy = 0; yy < h; yy++) {
        uint16_t *row = &s_buf[(ry + yy) * SCREEN_W + x];
        for (int xx = 0; xx < w; xx++) {
            uint16_t v = gfx_swap(row[xx]);
            int r = ((v >> 11) * ia + cr * alpha) >> 8;
            int g = (((v >> 5) & 63) * ia + cg * alpha) >> 8;
            int b = ((v & 31) * ia + cb * alpha) >> 8;
            row[xx] = gfx_swap((uint16_t)((r << 11) | (g << 5) | b));
        }
    }
}

void gfx_blit(const sprite_t *s, int x, int y)
{
    if (y + s->h <= s_y0 || y >= s_y0 + s_rows || x + s->w <= 0 || x >= SCREEN_W) {
        return;
    }
    int ry0 = y - s_y0;
    for (int sy = 0; sy < s->h; sy++) {
        int ry = ry0 + sy;
        if (ry < 0 || ry >= s_rows) {
            continue;
        }
        const uint16_t *src = &s->px[sy * s->w];
        uint16_t *dst = &s_buf[ry * SCREEN_W];
        for (int sx = 0; sx < s->w; sx++) {
            int dx = x + sx;
            if (dx < 0 || dx >= SCREEN_W) {
                continue;
            }
            uint16_t v = src[sx];
            if (v != GFX_TRANS) {
                dst[dx] = v;
            }
        }
    }
}

void gfx_blit_ex(const sprite_t *s, int x, int y, bool flip, bool silhouette, uint16_t color)
{
    if (y + s->h <= s_y0 || y >= s_y0 + s_rows || x + s->w <= 0 || x >= SCREEN_W) {
        return;
    }
    int ry0 = y - s_y0;
    for (int sy = 0; sy < s->h; sy++) {
        int ry = ry0 + sy;
        if (ry < 0 || ry >= s_rows) {
            continue;
        }
        const uint16_t *src = &s->px[sy * s->w];
        uint16_t *dst = &s_buf[ry * SCREEN_W];
        for (int sx = 0; sx < s->w; sx++) {
            int dx = x + sx;
            if (dx < 0 || dx >= SCREEN_W) {
                continue;
            }
            uint16_t v = src[flip ? s->w - 1 - sx : sx];
            if (v != GFX_TRANS) {
                dst[dx] = silhouette ? color : v;
            }
        }
    }
}

void gfx_blit_silhouette(const sprite_t *s, int x, int y, uint16_t color)
{
    if (y + s->h <= s_y0 || y >= s_y0 + s_rows || x + s->w <= 0 || x >= SCREEN_W) {
        return;
    }
    int ry0 = y - s_y0;
    for (int sy = 0; sy < s->h; sy++) {
        int ry = ry0 + sy;
        if (ry < 0 || ry >= s_rows) {
            continue;
        }
        const uint16_t *src = &s->px[sy * s->w];
        uint16_t *dst = &s_buf[ry * SCREEN_W];
        for (int sx = 0; sx < s->w; sx++) {
            int dx = x + sx;
            if (dx < 0 || dx >= SCREEN_W) {
                continue;
            }
            if (src[sx] != GFX_TRANS) {
                dst[dx] = color;
            }
        }
    }
}

static int glyph_index(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');   /* the font is capitals only */
    }
    const char *p = strchr(g_font_order, ch);
    return p ? (int)(p - g_font_order) : -1;
}

void gfx_text(const char *text, int x, int y, int scale, uint16_t color)
{
    if (y + 7 * scale <= s_y0 || y >= s_y0 + s_rows) {
        return;   /* nothing of this line in the band */
    }
    for (; *text; text++) {
        int gi = glyph_index(*text);
        if (gi < 0) {
            gi = glyph_index('?');
        }
        const uint8_t *glyph = g_font[gi];
        for (int row = 0; row < 7; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 5; col++) {
                if (bits & (1u << col)) {
                    gfx_fill(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

int gfx_text_w(const char *text, int scale)
{
    int n = 0;
    while (text[n]) {
        n++;
    }
    return n ? n * 6 * scale - scale : 0;
}
