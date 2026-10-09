#include "art.h"

#include <math.h>
#include <stdio.h>

#include "gfx.h"

#define BLACK PX(0, 0, 0)
#define WHITE PX(255, 255, 255)
#define GOLD PX(255, 224, 102)
#define SKIN PX(242, 201, 160)
#define NAVY PX(29, 43, 79)
#define BLUE PX(47, 79, 143)
#define ORANGE PX(255, 122, 26)
#define LEGS PX(35, 42, 64)
#define SHOE PX(17, 17, 17)
#define SKY PX(15, 21, 48)

static int s_band0, s_band1;   /* rows of the band being drawn */
static uint16_t c_wall, c_stripe, c_dado, c_floor, c_floor_line, c_recess, c_pool, c_win_frame;

static const int8_t SIN16[16] = { 0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3 };   /* x8 */

static inline void R(int x, int y, int w, int h, uint16_t c)
{
    gfx_fill(x, y, w, h, c);
}

static inline bool band_hits(int y0, int y1)
{
    return y1 > s_band0 && y0 < s_band1;
}

static uint16_t pal(const world_t *w, int i)
{
    const uint8_t *c = w->L->pal[i];
    return gfx_rgb(c[0], c[1], c[2]);
}

void art_level(const world_t *w)
{
    c_wall = pal(w, 0);
    c_stripe = pal(w, 1);
    c_dado = pal(w, 2);
    c_floor = pal(w, 3);
    c_floor_line = gfx_mix(c_floor, BLACK, 85);
    c_recess = gfx_mix(c_wall, BLACK, 85);
    c_pool = gfx_mix(c_floor, PX(255, 238, 170), 70);
    c_win_frame = PX(27, 36, 64);
}

/* ---- building --------------------------------------------------------------- */

static void draw_floor(const world_t *w, int f, int cam)
{
    int top = w->y0 + f * FLOOR_H;
    R(0, top, SCREEN_W, FLOOR_H, c_wall);
    for (int x = -(cam % 8); x < SCREEN_W; x += 8) {
        R(x, top + 4, 4, 40, c_stripe);
    }
    R(0, top + 44, SCREEN_W, 14, c_dado);
    R(0, top + 58, SCREEN_W, 6, c_floor);
    R(0, top + 58, SCREEN_W, 1, c_floor_line);
    R(0, top, SCREEN_W, 3, PX(26, 26, 34));
    for (int col = 2; col < w->w; col += 6) {   /* moonlit windows */
        int x = col * TILE - cam;
        if (x < -24 || x > SCREEN_W) continue;
        R(x, top + 12, 18, 22, c_win_frame);
        R(x + 1, top + 13, 16, 20, PX(43, 63, 110));
        R(x + 8, top + 13, 2, 20, c_win_frame);
        R(x + 3, top + 15, 3, 3, PX(232, 232, 192));
    }
    for (int col = 5; col < w->w; col += 8) {   /* ceiling lamps and their pool on the floor */
        int x = col * TILE - cam;
        if (x < -40 || x > SCREEN_W + 40) continue;
        R(x + 4, top + 3, 8, 3, PX(204, 204, 204));
        R(x - 8, top + 58, 32, 6, c_pool);
    }
}

static void draw_hide(const world_t *w, const cell_t *h, int cam)
{
    int x = h->col * TILE - cam, top = w->y0 + h->f * FLOOR_H;
    if (x < -20 || x > SCREEN_W) return;
    R(x - 1, top + 6, 18, 52, c_recess);
    R(x - 3, top + 4, 3, 54, PX(201, 191, 168));
    R(x + 16, top + 4, 3, 54, PX(201, 191, 168));
    R(x - 4, top + 4, 26, 3, PX(221, 211, 187));
}

static void draw_stairs(const world_t *w, const stairs_t *s, int cam)
{
    int x = s->col * TILE - cam, top = w->y0 + s->f * FLOOR_H;
    if (x < -40 || x > SCREEN_W + 20) return;
    uint16_t arrow = PX(150, 132, 70);
    if (s->dir < 0) {
        for (int i = 0; i < 7; i++) R(x - 8 + i * 4, top + 52 - i * 8, 10, 4, PX(138, 112, 88));
        for (int i = 0; i < 3; i++) R(x + 7 - i, top + 22 + i, 2 + i * 2, 1, arrow);
    } else {
        R(x - 6, top + 58, 28, 6, PX(26, 20, 16));
        R(x - 6, top + 40, 2, 18, PX(176, 160, 128));
        R(x + 20, top + 40, 2, 18, PX(176, 160, 128));
        R(x - 6, top + 40, 28, 2, PX(176, 160, 128));
        for (int i = 0; i < 3; i++) R(x + 5 + i, top + 22 + i, 6 - i * 2, 1, arrow);
    }
}

static void draw_door(const world_t *w, const door_t *d, int cam)
{
    int x = d->col * TILE + 8 - cam, top = w->y0 + d->f * FLOOR_H;
    if (x < -30 || x > SCREEN_W + 30) return;
    R(x - 9, top + 10, 18, 48, PX(42, 31, 24));
    if (!d->open) {
        R(x - 3, top + 12, 6, 46, PX(139, 90, 43));
        R(x - 3, top + 12, 6, 2, PX(168, 116, 61));
        R(x + (d->side > 0 ? 1 : -2), top + 34, 1, 3, PX(255, 215, 0));
    } else {
        float a = d->anim > 0 ? d->anim : 0;
        int wd = (int)(14 * (1 - a / 0.25f)) + 2;
        int px = d->side > 0 ? x + 3 : x - 3 - wd;
        R(px, top + 12, wd, 46, PX(107, 68, 32));
        R(px, top + 12, wd, 2, PX(168, 116, 61));
    }
}

static void draw_exit(const world_t *w, int cam)
{
    if (w->exit_col < 0) return;
    int x = w->exit_col * TILE - cam, top = w->y0;
    bool open = world_loot_left(w) == 0;
    uint16_t steel = PX(154, 170, 170);
    for (int i = 0; i < 6; i++) R(x + 2, top + 8 + i * 9, 12, 2, steel);
    R(x + 1, top + 6, 2, 52, steel);
    R(x + 13, top + 6, 2, 52, steel);
    R(x - 5, top + 48, 26, 10, open ? PX(46, 139, 87) : PX(107, 34, 34));
    if (open) {
        gfx_text("EXIT", x - 3, top + 50, 1, WHITE);
    } else {   /* a little padlock */
        R(x + 5, top + 52, 6, 5, GOLD);
        R(x + 6, top + 49, 4, 1, GOLD);
        R(x + 5, top + 50, 1, 2, GOLD);
        R(x + 10, top + 50, 1, 2, GOLD);
    }
}

static void draw_loot(const world_t *w, const loot_t *l, int cam, uint32_t frame)
{
    if (l->state != LS_FLOOR) return;
    int x = (int)l->x - cam, y = (int)feet_y(w, l->f) - 10;
    if (x < -12 || x > SCREEN_W) return;
    int bob = SIN16[(frame / 2 + (int)l->x) & 15] / 5;
    y += bob;
    switch (l->kind) {
    case LOOT_GOLD:
    case LOOT_BAR:
        R(x, y + 4, 10, 5, PX(184, 144, 30));
        R(x + 1, y + 3, 8, 4, PX(255, 216, 74));
        break;
    case LOOT_PAINTING:
        R(x - 1, y - 4, 12, 12, PX(122, 90, 42));
        R(x + 1, y - 2, 8, 8, PX(224, 122, 74));
        R(x + 3, y + 2, 4, 3, PX(68, 170, 119));
        break;
    case LOOT_VASE:
        R(x + 2, y, 6, 9, PX(90, 160, 224));
        R(x + 3, y - 2, 4, 2, PX(90, 160, 224));
        R(x + 3, y + 3, 4, 2, PX(220, 236, 250));
        break;
    case LOOT_WATCH:
        R(x + 3, y - 1, 4, 11, PX(90, 64, 48));
        R(x + 1, y + 2, 8, 6, PX(192, 200, 208));
        R(x + 4, y + 4, 2, 2, PX(51, 51, 51));
        break;
    default: {   /* jade, jewel: a gem cross */
        uint16_t c = l->kind == LOOT_JADE ? PX(79, 209, 139) : PX(255, 95, 160);
        R(x + 2, y + 2, 6, 6, c);
        R(x + 4, y, 2, 10, c);
        R(x, y + 4, 10, 2, c);
        break;
    }
    }
    if (((frame / 10) + (int)l->x) % 3 == 0) R(x + 8, y - 2, 2, 2, WHITE);
}

static void draw_props(const world_t *w, int cam, const bool *vis)
{
    for (int i = 0; i < w->ncans; i++) {
        const can_t *c = &w->cans[i];
        if (!vis[c->f]) continue;
        int x = (int)c->x - cam - 3, y = (int)feet_y(w, c->f) - 6;
        R(x, y, 5, 6, PX(204, 51, 51));
        R(x, y + 1, 5, 1, PX(238, 238, 238));
    }
    for (int i = 0; i < w->ncrates; i++) {
        const cell_t *k = &w->crates[i];
        if (!vis[k->f]) continue;
        int x = k->col * TILE - cam + 1, y = (int)feet_y(w, k->f) - CRATE_H;
        uint16_t dark = PX(122, 74, 32);
        R(x, y, 14, CRATE_H, PX(154, 106, 58));
        R(x, y, 14, 2, PX(192, 136, 80));
        R(x + 1, y + 6, 12, 2, dark);
        R(x + 1, y + 2, 2, CRATE_H - 2, dark);
        R(x + 11, y + 2, 2, CRATE_H - 2, dark);
    }
    for (int i = 0; i < w->npeels; i++) {
        const peel_t *e = &w->peels[i];
        if (!vis[e->f]) continue;
        int x = (int)e->x - cam - 5, y = (int)feet_y(w, e->f) - 3;
        uint16_t yel = PX(245, 214, 58);
        R(x, y, 10, 3, yel);
        R(x - 2, y + 1, 3, 2, yel);
        R(x + 9, y + 1, 3, 2, yel);
        R(x + 4, y - 1, 2, 2, PX(138, 106, 32));
    }
    for (int i = 0; i < w->nsprings; i++) {
        const spring_t *b = &w->springs[i];
        if (!vis[b->f]) continue;
        int x = b->col * TILE - cam, y = (int)feet_y(w, b->f), sq = b->anim > 0 ? 3 : 0;
        R(x + 1, y - 3, 14, 3, PX(85, 85, 85));
        for (int k = 0; k < 3; k++) R(x + 3, y - 5 - k * (3 - sq / 3), 10, 1, PX(204, 204, 204));
        R(x, y - 12 + sq, 16, 3, PX(224, 64, 64));
        R(x, y - 12 + sq, 16, 1, PX(255, 144, 144));
    }
    for (int i = 0; i < w->nhatches; i++) {
        const cell_t *h = &w->hatches[i];
        if (!vis[h->f]) continue;
        int x = h->col * TILE - cam, y = (int)feet_y(w, h->f);
        R(x - 2, y, 20, 6, PX(20, 16, 12));
        for (int k = 0; k < 5; k++) R(x - 1 + k * 4, y + 1, 2, 4, PX(90, 90, 96));
        R(x - 2, y, 20, 1, PX(138, 138, 144));
    }
}

/* ---- people ------------------------------------------------------------------ */

void art_thief(int x, int y, int face, int step, int carry, bool air, bool hidden)
{
    uint16_t stripe_a = hidden ? PX(14, 20, 38) : NAVY, stripe_b = hidden ? PX(110, 110, 120) : PX(242, 242, 242);
    uint16_t skin = hidden ? PX(110, 92, 74) : SKIN, hat = hidden ? PX(120, 60, 14) : ORANGE;
    if (air) {   /* legs tucked */
        R(x + 2, y - 10, 9, 4, LEGS);
        R(x + (face > 0 ? 8 : 0), y - 8, 5, 3, SHOE);
        R(x + (face > 0 ? 0 : 8), y - 6, 5, 3, SHOE);
    } else {
        R(x + 3, y - 10, 3, 10 - (step ? 2 : 0), LEGS);
        R(x + 7, y - 10, 3, 10 - (step ? 0 : 2), LEGS);
        R(x + 2 + (face > 0), y - 2, 5, 2, SHOE);
        R(x + 6 + (face > 0), y - 2, 5, 2, SHOE);
    }
    for (int i = 0; i < 6; i++) {   /* tall striped jumper */
        R(x + 2, y - 24 + (i * 12) / 5, 9, 3, i % 2 ? stripe_b : stripe_a);
    }
    R(x + 2, y - 33, 9, 9, skin);
    R(x + 2, y - 30, 9, 3, SHOE);   /* eye mask */
    R(x + (face > 0 ? 7 : 3), y - 29, 2, 1, WHITE);
    R(x + 1, y - 35, 11, 4, hat);
    R(x + 4, y - 37, 5, 2, hat);
    /* stun gun: a chunky toy-like blaster with a blue tip */
    R(x + (face > 0 ? 9 : -4), y - 20, 7, 3, PX(255, 204, 51));
    R(x + (face > 0 ? 15 : -5), y - 20, 2, 3, PX(90, 208, 255));
    R(x + (face > 0 ? 10 : 0), y - 17, 2, 3, PX(128, 96, 16));
    if (carry > 0) {   /* the loot sack grows with what you carry */
        int s = 6 + carry * 2;
        if (s > 12) s = 12;
        int sx = face > 0 ? x - s + 2 : x + BODY_W - 2;
        R(sx, y - 24, s, s, PX(200, 180, 138));
        R(sx + 2, y - 26, 4, 3, PX(168, 148, 106));
        R(sx + s / 2 - 1, y - 21, 2, s - 6, PX(106, 90, 58));
    }
}

static void stars(int cx, int y, uint32_t frame, int n, int r)
{
    for (int i = 0; i < n; i++) {
        int k = (int)(frame / 2 + i * 5) & 15;
        int sx = cx + SIN16[(k + 4) & 15] * r / 8, sy = y + SIN16[k] * 3 / 8;
        R(sx - 1, sy, 3, 1, GOLD);
        R(sx, sy - 1, 1, 3, GOLD);
    }
}

static void draw_guard(const world_t *w, const guard_t *g, int cam, uint32_t frame)
{
    int x = (int)g->x - cam, y = (int)feet_y(w, g->f);
    if (x < -30 || x > SCREEN_W + 30) return;
    bool st = g->state == G_STUNNED;
    int fr = g->face > 0;
    if (g->type == GT_DOG) {
        uint16_t fur = PX(138, 90, 48), dk = PX(106, 64, 32);
        R(x, y - 9, 13, 6, fur);
        R(x + (fr ? 10 : -3), y - 12, 6, 6, fur);
        R(x + (fr ? 14 : -3), y - 10, 2, 2, SHOE);
        R(x + 1, y - 3, 2, 3, dk);
        R(x + 9, y - 3, 2, 3, dk);
        R(x + (fr ? -2 : 13), y - 11, 2, 4, fur);
        if (st) stars(x + 6, y - 18, frame, 2, 6);
    } else if (g->state == G_SLEEP) {   /* slumped in a chair */
        uint16_t wood = PX(90, 58, 32);
        R(x - 2, y - 14, 16, 3, wood);
        R(x - 2, y - 14, 2, 14, wood);
        R(x + 12, y - 14, 2, 14, wood);
        R(x + 1, y - 26, 10, 12, BLUE);
        R(x + 2, y - 33, 9, 8, SKIN);
        R(x + 1, y - 35, 11, 3, NAVY);
        gfx_text("Z", x + 14, y - 44 - (int)((frame / 15) % 2) * 3, 1, PX(204, 221, 238));
        return;
    } else {
        int step = ((int)(g->walk_t * 7)) % 2;
        int lean = st ? 4 : 0;
        bool moving = g->vx != 0;
        R(x + 3, y - 10, 3, 10 - (step && moving ? 2 : 0), NAVY);
        R(x + 7, y - 10, 3, 10 - (step || !moving ? 0 : 2), NAVY);
        R(x + 1, y - 25, 11, 15, BLUE);
        R(x + 5, y - 22, 2, 2, PX(255, 215, 0));
        R(x + 2 + lean, y - 33, 9, 8, SKIN);
        R(x + 1 + lean, y - 36, 11, 4, NAVY);
        R(x + (fr ? 8 : -1) + lean, y - 33, 4, 2, NAVY);
        R(x + (fr ? 7 : 4) + lean, y - 30, 2, 2, SHOE);
        if (g->type == GT_SHIELD) {
            R(x + (fr ? 12 : -4), y - 28, 4, 24, PX(154, 168, 184));
            R(x + (fr ? 13 : -3), y - 26, 2, 20, PX(200, 212, 224));
        }
        if (g->state == G_ALERT && g->windup > 0) R(x + (fr ? 11 : -4), y - 21, 5, 2, PX(51, 51, 51));
        if (st) stars(x + 6, y - 42, frame, 3, 8);
    }
    if (!st) {   /* spot meter, alert mark, bubble */
        if (g->state == G_ALERT) {
            R(x + 5, y - 52, 3, 8, PX(255, 64, 64));
            R(x + 5, y - 42, 3, 2, PX(255, 64, 64));
        } else if (g->spot > 0.05f) {
            R(x - 1, y - 44, 14, 4, BLACK);
            R(x, y - 43, (int)(12 * g->spot), 2, PX(255, 176, 32));
        } else if (g->bubble) {
            static const char *const B[] = { "", "?", "!?", "HA!" };
            const char *t = B[g->bubble];
            gfx_text(t, x + 6 - gfx_text_w(t, 1) / 2, y - 50, 1, GOLD);
        }
    }
}

void art_balloon(int x, int y, uint32_t frame)
{
    static const uint8_t WID[6] = { 8, 14, 18, 18, 14, 8 };
    R(x - 2, y + 20, 4, 6, PX(122, 90, 58));
    for (int i = 0; i < 6; i++) {
        int wd = WID[i];
        for (int j = 0; j < wd; j += 3) {
            R(x - wd / 2 + j, y - 6 + i * 3, 3, 3, (j / 3) % 2 ? PX(242, 242, 242) : PX(224, 64, 64));
        }
    }
    R(x - 1, y + 12, 1, 8, PX(204, 204, 204));
    R(x + 1, y + 12, 1, 8, PX(204, 204, 204));
    R(x - 5, y + 20, 10, 6, PX(160, 120, 74));
    /* the partner: round, green cap, waving */
    R(x - 3, y + 14, 6, 6, SKIN);
    R(x - 4, y + 13, 8, 2, PX(46, 139, 87));
    R(x + 3 + SIN16[frame & 15] / 6, y + 15, 2, 4, SKIN);
}

/* ---- the scene ------------------------------------------------------------------ */

void art_world(const world_t *w, int cam, uint32_t frame, float escape_t)
{
    s_band0 = gfx_band_y0();
    s_band1 = s_band0 + gfx_band_rows();
    /* floors this band shows (a floor's people reach 16 px into the one above) */
    bool vis[MAX_FLOORS] = { false };
    bool any = false;
    for (int f = 0; f < w->nf; f++) {
        int top = w->y0 + f * FLOOR_H;
        vis[f] = band_hits(top - 16, top + FLOOR_H);
        any |= vis[f];
    }
    const thief_t *p = &w->p;
    int exit_x = w->exit_col * TILE + 8 - cam;
    float by = (float)(w->y0 - 22) - (escape_t > 0 ? escape_t * 18 : 0);

    /* sky over the roof */
    if (band_hits(HUD_H, w->y0)) {
        R(0, HUD_H, SCREEN_W, w->y0 - HUD_H, SKY);
        int sky_h = w->y0 - HUD_H - 6;
        for (int i = 0; i < 18 + sky_h / 3; i++) {
            int sx = ((i * 53 + 7 - cam * 3 / 10) % SCREEN_W + SCREEN_W) % SCREEN_W;
            R(sx, HUD_H + 2 + (i * 7) % sky_h, 1, 1, PX(170, 170, 190));
        }
        R(280 - (cam / 10) % 40, HUD_H + 3, 8, 8, PX(245, 240, 200));
        R(0, w->y0 - 4, SCREEN_W, 4, PX(58, 58, 68));
    }
    if (any) {
        for (int f = 0; f < w->nf; f++) {
            if (vis[f] && band_hits(w->y0 + f * FLOOR_H, w->y0 + (f + 1) * FLOOR_H)) draw_floor(w, f, cam);
        }
        for (int i = 0; i < w->nhides; i++) {
            if (vis[w->hides[i].f]) draw_hide(w, &w->hides[i], cam);
        }
        for (int i = 0; i < w->nstairs; i++) {
            if (vis[w->stairs[i].f]) draw_stairs(w, &w->stairs[i], cam);
        }
        if (vis[0]) draw_exit(w, cam);
    }
    if (w->exit_col >= 0 && band_hits((int)by - 8, (int)by + 28)) {
        art_balloon(exit_x, (int)by, frame);
    }
    if (any) {
        for (int i = 0; i < w->ndoors; i++) {
            if (vis[w->doors[i].f]) draw_door(w, &w->doors[i], cam);
        }
        for (int i = 0; i < w->nloot; i++) {
            if (vis[w->loot[i].f]) draw_loot(w, &w->loot[i], cam, frame);
        }
        draw_props(w, cam, vis);
        for (int i = 0; i < w->nlights; i++) {   /* bonus spotlights: the one real blend */
            const light_t *s = &w->lights[i];
            if (!vis[s->f]) continue;
            int top = w->y0 + s->f * FLOOR_H, x = (int)s->x - cam;
            gfx_blend(x, top + 4, s->w, 54, PX(255, 246, 160), 72);
            R(x + 4, top + 56, s->w - 8, 2, PX(255, 246, 160));
            R(x + s->w / 2 - 4, top + 3, 8, 3, PX(221, 221, 221));
        }
        for (int i = 0; i < w->nguards; i++) {
            if (vis[w->guards[i].f]) draw_guard(w, &w->guards[i], cam, frame);
        }
    }

    int px = (int)p->x - cam, feet = (int)feet_y(w, p->f);
    if (escape_t < 0) {
        bool blink = p->inv > 0 && (frame / 2) % 2;
        if (p->knock > 0) {   /* flat on the floor, seeing stars */
            R(px - 6, feet - 6, 24, 6, NAVY);
            R(px + 14, feet - 9, 8, 8, SKIN);
            R(px + 13, feet - 11, 10, 3, ORANGE);
            stars(px + 18, feet - 18, frame, 2, 6);
        } else if (!blink) {
            int step = ((int)(p->walk_t * 8)) % 2;
            art_thief(px, feet - (int)p->y, p->face, step, p->ncarry, p->air, p->hidden);
            if (p->slip > 0) stars(px + 6, feet - 44, frame, 2, 6);
        }
        if (p->muzzle > 0) R(px + 6 + (p->face > 0 ? 11 : -17), feet - (int)p->y - 22, 6, 6, PX(191, 244, 255));
    } else if (escape_t < 0.8f) {
        art_thief(exit_x - 6, (int)feet_y(w, 0), 1, (int)(escape_t * 8) % 2, p->ncarry, false, false);
    } else {   /* aboard: a head with the orange beanie in the basket */
        R(exit_x - 3, (int)by + 14 - 6, 6, 6, SKIN);
        R(exit_x - 4, (int)by + 12 - 6, 8, 3, ORANGE);
    }

    for (int i = 0; i < w->ndarts; i++) {
        const shot_t *s = &w->darts[i];
        R((int)s->x - cam - 2, (int)feet_y(w, s->f) - DART_H, 5, 2, PX(153, 255, 102));
    }
    for (int i = 0; i < w->nbolts; i++) {   /* a glowing blue pellet with a short trail */
        const shot_t *b = &w->bolts[i];
        int bx = (int)b->x - cam, y = (int)feet_y(w, b->f) - 20;
        R(bx - 3 - (b->vx > 0 ? 5 : -5), y, 4, 2, PX(45, 104, 128));
        R(bx - 3, y - 1, 6, 4, PX(90, 208, 255));
        R(bx - 2, y, 4, 2, WHITE);
    }
    for (int i = 0; i < w->nfx; i++) {
        const fx_t *e = &w->fx[i];
        gfx_text(e->text, (int)e->x - cam - gfx_text_w(e->text, 1) / 2, w->y0 + (int)e->y, 1, e->color);
    }
    if (w->fade > 0.1f) {   /* changing floor: a short cut to black, no blend */
        R(0, w->y0, SCREEN_W, SCREEN_H - w->y0, BLACK);
    }
}

static void heart(int x, int y, uint16_t c)
{
    R(x, y + 1, 7, 2, c);
    R(x + 1, y, 2, 1, c);
    R(x + 4, y, 2, 1, c);
    R(x + 1, y + 3, 5, 1, c);
    R(x + 2, y + 4, 3, 1, c);
    R(x + 3, y + 5, 1, 1, c);
}

void art_hud(const world_t *w, int cam, int total, uint32_t frame)
{
    char buf[24];
    R(0, 0, SCREEN_W, HUD_H, PX(16, 16, 20));
    R(0, HUD_H - 1, SCREEN_W, 1, PX(51, 51, 51));
    for (int i = 0; i < w->lives && i < 5; i++) heart(3 + i * 9, 2, PX(255, 90, 90));
    int tl = (int)ceilf(w->time_left);
    bool low = w->time_left < 30 && (frame / 4) % 2;
    snprintf(buf, sizeof(buf), "%d:%02d", tl / 60, tl % 60);
    gfx_text(buf, 3, 12, 1, low ? PX(255, 64, 64) : WHITE);
    snprintf(buf, sizeof(buf), "%s %d/%d", w->L->bonus ? "GOLD" : "LOOT", world_loot_carried(w), w->nloot);
    gfx_text(buf, 50, 3, 1, PX(255, 216, 74));
    snprintf(buf, sizeof(buf), "%.12s", w->L->name);
    gfx_text(buf, 50, 13, 1, PX(187, 187, 187));
    /* mini map: one strip per floor, loot dots, the exit, the view, the thief */
    int mx = 124, mw = 104, mh = 18;
    R(mx, 2, mw, mh, PX(30, 34, 51));
    int lw = w->w * TILE, rh = mh / w->nf;
    for (int f = 1; f < w->nf; f++) R(mx, 2 + f * rh - 1, mw, 1, PX(51, 58, 85));
    for (int i = 0; i < w->nloot; i++) {
        const loot_t *l = &w->loot[i];
        if (l->state == LS_FLOOR) R(mx + (int)l->x * mw / lw, 2 + l->f * rh + rh / 2 - 1, 2, 2, PX(255, 216, 74));
    }
    if (w->exit_col >= 0) {
        R(mx + w->exit_col * TILE * mw / lw, 2 + rh / 2 - 2, 3, 3, world_loot_left(w) ? PX(170, 51, 51) : PX(51, 204, 51));
    }
    R(mx + cam * mw / lw, 2, 1, mh, PX(90, 96, 120));
    R(mx + (cam + SCREEN_W) * mw / lw - 1, 2, 1, mh, PX(90, 96, 120));
    if ((frame / 4) % 2) R(mx + (int)thief_cx(w) * mw / lw - 1, 2 + w->p.f * rh + rh / 2 - 2, 3, 3, WHITE);
    snprintf(buf, sizeof(buf), "%06d", total);
    gfx_text(buf, SCREEN_W - 3 - gfx_text_w(buf, 2), 4, 2, GOLD);
}
