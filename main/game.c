#include "game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "art.h"
#include "boot.h"
#include "config.h"
#include "display.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"
#include "input.h"
#include "levels.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sound.h"
#include "world.h"

static const char *TAG = "heist";

#define FRAME_US 33333
#define FRAME_DT (FRAME_US / 1e6f)
#define MAX_CATCHUP 3

static int64_t s_stat_t0, s_stat_render_us, s_stat_render_max;
static int s_stat_frames, s_stat_late;

typedef enum { ST_TITLE, ST_INTRO, ST_PLAY, ST_PAUSE, ST_ESCAPE, ST_WIN, ST_LOSE, ST_OVER } state_t;

static world_t s_w;                   /* the whole level: ~2 KB, static */

static state_t s_state = ST_TITLE;
static float s_t;                     /* seconds in the current state */
static uint32_t s_frame;
static int s_level, s_total, s_hiscore, s_lives = LIVES;
static uint8_t s_stars[MAX_LEVELS];   /* best stars per level, 0 = not cleared yet */
static int s_unlocked;                /* highest level that may be played (strictly in order) */
static bool s_reset_armed;            /* RESET PROGRESS asks for a second OK */
static float s_cam, s_cam_y;
static bool s_ok_wait_release;        /* the press that opened a state is not a press in it */
static bool s_swap_keys;              /* false: UP walks left, DOWN walks right */
static bool s_hurry;
static int s_menu_sel;
static const char *s_popup;
static float s_popup_t;
static bool s_assistant, s_dim, s_screen_off;
static bool s_redraw = true;          /* static screens (pause, results) draw only on change */
static wresult_t s_result;

/* ---- NVS ("heist"): high score, progress, sound, keys ------------------- */

static nvs_handle_t s_nvs;

/* Progress is one small blob: the highest unlocked level and the best stars
 * per level, so a reboot (or a trip to the assistant) keeps both. */
#define PROGRESS_VER 2
typedef struct {
    uint8_t ver, unlocked, count;
    uint8_t stars[MAX_LEVELS];
} progress_t;

static void progress_save(void)
{
    if (!s_nvs) {
        return;
    }
    progress_t pr = { .ver = PROGRESS_VER, .unlocked = (uint8_t)s_unlocked, .count = (uint8_t)g_level_count };
    memcpy(pr.stars, s_stars, sizeof(pr.stars));
    nvs_set_blob(s_nvs, "progress", &pr, sizeof(pr));
    nvs_commit(s_nvs);
}

static void progress_load(void)
{
    progress_t pr;
    size_t n = sizeof(pr);
    if (nvs_get_blob(s_nvs, "progress", &pr, &n) == ESP_OK && n == sizeof(pr) && pr.ver == PROGRESS_VER) {
        s_unlocked = pr.unlocked < g_level_count ? pr.unlocked : g_level_count - 1;
        for (int i = 0; i < MAX_LEVELS; i++) {
            s_stars[i] = pr.stars[i] <= 3 ? pr.stars[i] : 0;
        }
        return;
    }
    /* First run of this version: the old per-level star keys belonged to the
     * three-level layout, so they are dropped; everything starts at 1-1. */
    for (int i = 0; i < 8; i++) {
        char key[12];
        snprintf(key, sizeof(key), "stars%d", i);
        nvs_erase_key(s_nvs, key);
    }
    s_unlocked = 0;
    memset(s_stars, 0, sizeof(s_stars));
    progress_save();
}

static void progress_reset(void)
{
    s_unlocked = 0;
    memset(s_stars, 0, sizeof(s_stars));
    progress_save();
}

static int total_stars(void)
{
    int n = 0;
    for (int i = 0; i < g_level_count && i < MAX_LEVELS; i++) {
        n += s_stars[i];
    }
    return n;
}

static void settings_load(void)
{
    if (nvs_open("heist", NVS_READWRITE, &s_nvs) != ESP_OK) {
        s_nvs = 0;
        return;
    }
    uint32_t v = 0;
    if (nvs_get_u32(s_nvs, "hiscore", &v) == ESP_OK) {
        s_hiscore = (int)v;
    }
    uint8_t u = 0;
    if (nvs_get_u8(s_nvs, "swapkeys", &u) == ESP_OK) {
        s_swap_keys = u != 0;
    }
    progress_load();
}

static void nvs_put_u8(const char *key, uint8_t v)
{
    if (s_nvs) {
        nvs_set_u8(s_nvs, key, v);
        nvs_commit(s_nvs);
    }
}

static sound_mode_t sound_mode_load(void)
{
    uint8_t v = SOUND_ALL;
    if (s_nvs) {
        nvs_get_u8(s_nvs, "sound", &v);
    }
    return v < SOUND_MODES ? (sound_mode_t)v : SOUND_ALL;
}

static void cycle_sound(void)
{
    sound_mode_t m = (sound_mode_t)((sound_mode() + 1) % SOUND_MODES);
    sound_set_mode(m);
    sound_play(SFX_SELECT);
    nvs_put_u8("sound", (uint8_t)m);
}

static void toggle_keys(void)
{
    s_swap_keys = !s_swap_keys;
    sound_play(SFX_SELECT);
    nvs_put_u8("swapkeys", s_swap_keys);
}

static void hiscore_save(int score)
{
    if (score <= s_hiscore) {
        return;
    }
    s_hiscore = score;
    if (s_nvs) {
        nvs_set_u32(s_nvs, "hiscore", (uint32_t)s_hiscore);
        nvs_commit(s_nvs);
    }
}

/* ---- drawing helpers ------------------------------------------------------ */

#define WHITE PX(255, 255, 255)
#define SHADOW PX(16, 16, 24)
#define GOLD PX(255, 224, 102)
#define GREY PX(160, 160, 170)
#define RED PX(255, 90, 58)
#define GREEN PX(155, 217, 58)
#define ORANGE PX(255, 122, 26)
#define SKY PX(15, 21, 48)

static void text_shadow(const char *text, int x, int y, int scale, uint16_t color)
{
    gfx_text(text, x + 1, y + 1, scale, SHADOW);
    gfx_text(text, x, y, scale, color);
}

static void text_center(const char *text, int y, int scale, uint16_t color)
{
    text_shadow(text, (SCREEN_W - gfx_text_w(text, scale)) / 2, y, scale, color);
}

static void meter(int x, int y, int w, float fill, uint16_t color)
{
    fill = fill < 0 ? 0 : fill > 1 ? 1 : fill;
    gfx_fill(x - 1, y - 1, w + 2, 7, PX(0, 0, 0));
    gfx_fill(x, y, w, 5, PX(50, 50, 58));
    gfx_fill(x, y, (int)(w * fill + 0.5f), 5, color);
}

static void panel(int y, int h)
{
    gfx_blend(28, y, SCREEN_W - 56, h, PX(10, 10, 14), 215);
    gfx_fill(28, y, SCREEN_W - 56, 1, GOLD);
    gfx_fill(28, y + h - 1, SCREEN_W - 56, 1, GOLD);
}

static void disc(int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        int h = 0;
        while ((h + 1) * (h + 1) + dy * dy <= r * r) h++;
        gfx_fill(cx - h, cy + dy, 2 * h + 1, 1, c);
    }
}

/* A chunky five-point star from rectangles, centred on (cx, cy). */
static void star_icon(int cx, int cy, uint16_t c)
{
    gfx_fill(cx - 1, cy - 9, 3, 4, c);
    gfx_fill(cx - 2, cy - 5, 5, 2, c);
    gfx_fill(cx - 9, cy - 3, 19, 3, c);
    gfx_fill(cx - 6, cy, 13, 2, c);
    gfx_fill(cx - 5, cy + 2, 11, 2, c);
    gfx_fill(cx - 6, cy + 4, 5, 2, c);
    gfx_fill(cx + 2, cy + 4, 5, 2, c);
    gfx_fill(cx - 7, cy + 6, 3, 2, c);
    gfx_fill(cx + 5, cy + 6, 3, 2, c);
}

static void popup(const char *text, float seconds)
{
    s_popup = text;
    s_popup_t = seconds;
}

static void stars_text(char *buf, int n)
{
    for (int i = 0; i < 3; i++) {
        buf[i] = i < n ? '*' : '-';
    }
    buf[3] = 0;
}

/* ---- menus ----------------------------------------------------------------- */

typedef enum { M_LEVEL, M_RESUME, M_RESTART, M_SOUND, M_KEYS, M_RESET, M_QUIT, M_ASSISTANT } mkind_t;
typedef struct {
    mkind_t kind;
    int arg;
} mitem_t;
#define MENU_MAX (MAX_LEVELS + 6)
#define TITLE_ROWS 10                 /* level list rows on the title; it scrolls */

static int menu_items(mitem_t *items)
{
    int n = 0;
    if (s_state == ST_TITLE) {
        for (int i = 0; i < g_level_count && i < MAX_LEVELS; i++) {
            items[n++] = (mitem_t){ M_LEVEL, i };
        }
    } else {
        items[n++] = (mitem_t){ M_RESUME, 0 };
        items[n++] = (mitem_t){ M_RESTART, 0 };
    }
    items[n++] = (mitem_t){ M_SOUND, 0 };
    items[n++] = (mitem_t){ M_KEYS, 0 };
    if (s_state == ST_TITLE) {
        items[n++] = (mitem_t){ M_RESET, 0 };
    }
    if (s_state == ST_PAUSE) {
        items[n++] = (mitem_t){ M_QUIT, 0 };
    }
    if (s_assistant) {
        items[n++] = (mitem_t){ M_ASSISTANT, 0 };
    }
    return n;
}

static void menu_label(const mitem_t *it, char *buf, size_t n)
{
    switch (it->kind) {
    case M_LEVEL: {
        char st[4];
        if (it->arg > s_unlocked) {
            snprintf(buf, n, "%-20s LOCKED", g_levels[it->arg].name);
        } else {
            stars_text(st, s_stars[it->arg]);
            snprintf(buf, n, "%-20s %s", g_levels[it->arg].name, st);
        }
        break;
    }
    case M_RESUME: snprintf(buf, n, "RESUME"); break;
    case M_RESTART: snprintf(buf, n, "RESTART LEVEL"); break;
    case M_SOUND: snprintf(buf, n, "%s", sound_mode_name(sound_mode())); break;
    case M_KEYS: snprintf(buf, n, s_swap_keys ? "KEYS: UP=RIGHT DOWN=LEFT" : "KEYS: UP=LEFT DOWN=RIGHT"); break;
    case M_RESET: snprintf(buf, n, s_reset_armed ? "RESET PROGRESS? OK = YES" : "RESET PROGRESS"); break;
    case M_QUIT: snprintf(buf, n, "QUIT TO TITLE"); break;
    case M_ASSISTANT: snprintf(buf, n, "BACK TO ASSISTANT"); break;
    }
}

/* ---- states ---------------------------------------------------------------- */

static void set_state(state_t st, bool ok_held)
{
    s_state = st;
    s_t = 0;
    s_redraw = true;
    s_ok_wait_release = ok_held;
    s_w.ok_active = s_w.ok_used = false;
    s_w.p.hidden = false;
    s_w.p.push = 0;
}

static void start_level(int i, bool ok_held)
{
    s_level = i;
    world_load(&s_w, &g_levels[i], s_lives);
    art_level(&s_w);
    s_cam = 0;
    s_cam_y = (float)s_w.y0;
    s_popup_t = 0;
    s_hurry = false;
    sound_set_hurry(false);
    sound_music(MUSIC_NONE);
    set_state(ST_INTRO, ok_held);
}

static void begin_play(bool ok_held)
{
    sound_music(MUSIC_SNEAK);
    set_state(ST_PLAY, ok_held);
}

static void finish(void)
{
    world_result(&s_w, &s_result);
    s_total += s_result.total;
    bool changed = false;
    if (s_level < MAX_LEVELS && s_result.stars > s_stars[s_level]) {
        s_stars[s_level] = (uint8_t)s_result.stars;
        changed = true;
    }
    if (s_level == s_unlocked && s_unlocked + 1 < g_level_count) {   /* finishing a level opens the next */
        s_unlocked++;
        changed = true;
    }
    if (changed) {
        progress_save();
    }
    hiscore_save(s_total);
    sound_set_hurry(false);
    if (s_result.bonus && !s_result.all) {
        sound_music(MUSIC_FAIL);
    } else {
        sound_music(s_level + 1 < g_level_count ? MUSIC_CLEAR : MUSIC_WIN);
    }
    set_state(ST_WIN, true);
}

static void lose(bool over)
{
    s_lives = s_w.lives;
    sound_set_hurry(false);
    sound_music(MUSIC_FAIL);
    if (over) {
        hiscore_save(s_total + s_w.score);
    }
    set_state(over ? ST_OVER : ST_LOSE, true);
}

static void update_camera(float dt, float target)
{
    float lw = level_w(&s_w);
    if (lw <= SCREEN_W) {
        target = (lw - SCREEN_W) / 2;
    } else if (target < 0) {
        target = 0;
    } else if (target > lw - SCREEN_W) {
        target = lw - SCREEN_W;
    }
    float k = dt * 6;
    s_cam += (target - s_cam) * (k > 1 ? 1 : k);
    /* tall buildings: glide up / down to the thief's floor (the escape looks at the roof) */
    float ty = (float)world_view_y0(&s_w, s_state == ST_ESCAPE ? 0 : s_w.p.f);
    float ky = dt * 8;
    s_cam_y += (ty - s_cam_y) * (ky > 1 ? 1 : ky);
    if (fabsf(ty - s_cam_y) < 0.5f) {
        s_cam_y = ty;
    }
    s_w.y0 = (int)lroundf(s_cam_y);
}

static void update_play(const input_state_t *in, float dt)
{
    if (s_popup_t > 0) {
        s_popup_t -= dt;
    }
    if (!in->ok) {
        s_ok_wait_release = false;
    }
    winput_t wi = { 0 };
    wi.ok = in->ok && !s_ok_wait_release;
    wi.ok_pressed = in->ok_pressed && !s_ok_wait_release;
    if (!wi.ok) {   /* the ladder reads one key: a direction only counts without OK */
        if (in->up) {
            wi.dir = s_swap_keys ? 1 : -1;
        } else if (in->down) {
            wi.dir = s_swap_keys ? -1 : 1;
        }
    }
    int lives0 = s_w.lives;
    wevent_t ev = world_update(&s_w, &wi, dt);
    if (s_w.lives < lives0 && ev == EV_NONE) {
        popup(s_w.why, 1.4f);
    }
    switch (ev) {
    case EV_PAUSE:
        sound_play(SFX_PAUSE);
        s_menu_sel = 0;
        set_state(ST_PAUSE, false);
        return;
    case EV_ESCAPE:
        sound_music(MUSIC_NONE);
        sound_play(SFX_ESCAPE);
        set_state(ST_ESCAPE, true);
        return;
    case EV_BONUS_DONE:
        finish();
        return;
    case EV_TIME_UP:
        lose(false);
        return;
    case EV_BUSTED:
        lose(true);
        return;
    default:
        break;
    }
    bool hurry = s_w.any_alert || s_w.time_left < 30;
    if (hurry != s_hurry) {
        s_hurry = hurry;
        sound_set_hurry(hurry);
    }
    update_camera(dt, thief_cx(&s_w) - SCREEN_W / 2 + s_w.p.face * 24);
}

static void to_title(bool ok_held);

static void menu_choose(const mitem_t *it)
{
    switch (it->kind) {
    case M_LEVEL:
        if (it->arg > s_unlocked) {
            static char msg[40];
            snprintf(msg, sizeof(msg), "LOCKED: CLEAR %.12s FIRST", g_levels[it->arg - 1].name);
            sound_play(SFX_LOCKED);
            popup(msg, 2.0f);
            break;
        }
        sound_play(SFX_SELECT);
        s_total = 0;
        s_lives = LIVES;
        start_level(it->arg, true);
        break;
    case M_RESUME:
        sound_play(SFX_PAUSE);
        set_state(ST_PLAY, true);
        break;
    case M_RESTART:
        s_lives = s_w.lives;
        start_level(s_level, true);
        break;
    case M_SOUND:
        cycle_sound();
        break;
    case M_KEYS:
        toggle_keys();
        break;
    case M_RESET:
        if (!s_reset_armed) {
            s_reset_armed = true;   /* ask again: one stray OK must not wipe the stars */
            sound_play(SFX_TICK);
        } else {
            s_reset_armed = false;
            progress_reset();
            sound_play(SFX_SELECT);
            popup("PROGRESS RESET: BACK TO 1-1", 2.0f);
            s_menu_sel = 0;
        }
        break;
    case M_QUIT:
        hiscore_save(s_total + s_w.score);
        to_title(true);
        break;
    case M_ASSISTANT:
        hiscore_save(s_total + s_w.score);
        ESP_LOGI(TAG, "back to the assistant");
        boot_to_assistant();
        popup("ASSISTANT WON'T START", 2.0f);
        break;
    }
}

static void update_menu(const input_state_t *in)
{
    mitem_t items[MENU_MAX];
    int n = menu_items(items);
    if (s_menu_sel >= n) {
        s_menu_sel = 0;
    }
    if (!in->ok) {
        s_ok_wait_release = false;
    }
    if (in->up_pressed || in->down_pressed) {
        s_menu_sel = (s_menu_sel + (in->up_pressed ? n - 1 : 1)) % n;
        s_reset_armed = false;
        sound_play(SFX_TICK);
    }
    if (in->ok_pressed && !s_ok_wait_release) {
        menu_choose(&items[s_menu_sel]);
    }
}

static void to_title(bool ok_held)
{
    sound_music(MUSIC_NONE);
    s_menu_sel = s_unlocked;   /* the cursor waits on the next level to play */
    s_reset_armed = false;
    set_state(ST_TITLE, ok_held);
}

static void update_end(const input_state_t *in)
{
    if (!in->ok) {
        s_ok_wait_release = false;
    }
    if (s_t < 1.0f) {
        return;
    }
    if (in->ok_pressed && !s_ok_wait_release) {
        if (s_state == ST_OVER) {
            to_title(true);
        } else if (s_state == ST_LOSE) {
            start_level(s_level, true);
        } else if (s_level + 1 < g_level_count) {
            s_lives = s_w.lives;
            start_level(s_level + 1, true);
        } else {
            to_title(true);
        }
    } else if (in->up_pressed) {
        to_title(false);
    }
}

/* ---- frames ----------------------------------------------------------------- */

static const char *hint(void)
{
    static char buf[48];
    const world_t *w = &s_w;
    if (w->p.knock > 0 || w->p.air) {
        return NULL;
    }
    if (world_at_exit(w)) {
        int left = world_loot_left(w);
        if (!left) return "OK: TO THE BALLOON!";
        snprintf(buf, sizeof(buf), "FIND %d MORE ITEM%s FIRST", left, left > 1 ? "S" : "");
        return buf;
    }
    const stairs_t *st = world_at_stairs(w);
    if (st) return st->dir < 0 ? "OK: UPSTAIRS" : "OK: DOWNSTAIRS";
    const door_t *d = world_door_ahead(w);
    if (d) return d->open ? "OK: CLOSE THE DOOR" : "OK: OPEN (BONKS A GUARD BEHIND IT)";
    if (world_at_hide(w)) return w->p.hidden ? "HIDING... LET GO TO STEP OUT" : "HOLD OK: HIDE   TAP OK: STUN GUN";
    if (world_near_spring(w, 40)) return "STEP ON THE SPRINGBOARD: UP A FLOOR";
    if (world_near_crate(w, 34)) return "RUN + OK: JUMP (HOLD OK = HIGHER)";
    if (world_near_peel(w, 40)) return "BANANA! JUMP IT OR SLIP NOISILY";
    if (w->any_alert) return "SPOTTED! HIDE, STUN HIM, JUMP THE DARTS";
    if (w->L->bonus) return "DODGE THE LIGHTS - HOLD OK IN AN ALCOVE";
    if (w->L->tutorial) {
        return world_loot_left(w) ? "STAND + TAP OK: STUN GUN   RUN + OK: JUMP" : "ALL THE LOOT! UP TO THE ROOF EXIT";
    }
    return NULL;
}

static void draw_overlays(void)
{
    char buf[48];
    const level_t *L = s_w.L;
    if (s_state == ST_INTRO) {
        panel(64, 100);
        text_center(L->name, 74, 2, L->bonus ? GOLD : ORANGE);
        text_center(L->sub, 96, 1, GREY);
        if (L->bonus) {
            snprintf(buf, sizeof(buf), "GRAB THE GOLD IN %d SECONDS", L->time);
            text_center(buf, 112, 1, WHITE);
            text_center("STAY OUT OF THE SPOTLIGHTS", 124, 1, WHITE);
        } else {
            snprintf(buf, sizeof(buf), "STEAL %d ITEMS, ESCAPE BY THE ROOF", s_w.nloot);
            text_center(buf, 112, 1, WHITE);
            snprintf(buf, sizeof(buf), "TIME %d:%02d   LIVES %d", L->time / 60, L->time % 60, s_w.lives);
            text_center(buf, 124, 1, WHITE);
        }
        text_center("OK: GO", 146, 1, GOLD);
    } else if (s_state == ST_PAUSE) {
        mitem_t items[MENU_MAX];
        int n = menu_items(items);
        int h = 30 + n * 13;
        int y0 = (SCREEN_H - h) / 2 + 8;
        panel(y0, h);
        text_center("PAUSED", y0 + 7, 2, GOLD);
        for (int i = 0; i < n; i++) {
            menu_label(&items[i], buf, sizeof(buf));
            int y = y0 + 27 + i * 13;
            if (i == s_menu_sel) {
                gfx_blend(40, y - 3, SCREEN_W - 80, 12, WHITE, 40);
                text_shadow(">", 48, y, 1, GOLD);
            }
            text_shadow(buf, 60, y, 1, i == s_menu_sel ? WHITE : GREY);
        }
    } else if (s_state == ST_WIN) {
        const char *next = s_level + 1 < g_level_count ? "OK: NEXT   UP: TITLE" : "ALL DONE!  OK: TITLE";
        if (s_result.bonus) {
            panel(54, 134);
            text_center("BONUS OVER", 62, 2, GOLD);
            for (int i = 0; i < 3; i++) {
                star_icon(SCREEN_W / 2 - 20 + i * 20, 158, i < s_result.stars ? GOLD : PX(70, 70, 80));
            }
            text_center(s_w.why ? s_w.why : "", 88, 1, WHITE);
            snprintf(buf, sizeof(buf), "GOLD %d/%d", s_result.bars, s_result.of);
            text_center(buf, 104, 1, GOLD);
            if (s_result.all) {
                snprintf(buf, sizeof(buf), "ALL THE GOLD +%d", SCORE_ALLBARS);
                text_center(buf, 116, 1, GREEN);
            }
            snprintf(buf, sizeof(buf), "BONUS %d  TOTAL %d", s_result.total, s_total);
            text_center(buf, 134, 1, GOLD);
            if (s_t > 1.0f) text_center(next, 172, 1, WHITE);
        } else {
            panel(42, 160);
            text_center("ESCAPED!", 50, 2, GREEN);
            for (int i = 0; i < 3; i++) {
                star_icon(SCREEN_W / 2 - 30 + i * 30, 80, i < s_result.stars ? GOLD : PX(70, 70, 80));
            }
            snprintf(buf, sizeof(buf), "LOOT %d   TIME +%d", s_result.loot, s_result.time);
            text_center(buf, 98, 1, WHITE);
            if (s_result.ghost) {
                snprintf(buf, sizeof(buf), "NEVER SPOTTED +%d", s_result.ghost);
            } else {
                snprintf(buf, sizeof(buf), "ACTION %d", s_result.action);
            }
            text_center(buf, 110, 1, s_result.ghost ? PX(143, 208, 255) : GREY);
            snprintf(buf, sizeof(buf), "LEVEL %d  TOTAL %d", s_result.total, s_total);
            text_center(buf, 128, 1, GOLD);
            if (s_t > 1.0f) text_center(next, 180, 1, WHITE);
        }
    } else if (s_state == ST_LOSE || s_state == ST_OVER) {
        bool over = s_state == ST_OVER;
        panel(70, 96);
        text_center(over ? "GAME OVER" : "BUSTED!", 80, 2, RED);
        text_center(s_w.why ? s_w.why : "", 104, 1, WHITE);
        if (over) {
            snprintf(buf, sizeof(buf), "TOTAL %d   HI %d", s_total + s_w.score, s_hiscore);
        } else {
            snprintf(buf, sizeof(buf), "LIVES LEFT %d", s_w.lives);
        }
        text_center(buf, 118, 1, GOLD);
        if (s_t > 1.0f) text_center(over ? "OK: TITLE" : "OK: TRY AGAIN   UP: TITLE", 144, 1, GREY);
    }
}

static void draw_world_frame(void)
{
    int cam = (int)s_cam;
    float esc = s_state == ST_ESCAPE ? s_t : -1;
    const char *h = s_state == ST_PLAY ? hint() : NULL;
    float push = s_w.p.push;
    bool full = level_w(&s_w) >= SCREEN_W;
    for (int y = 0; y < SCREEN_H; y += BAND_ROWS) {
        int rows = SCREEN_H - y < BAND_ROWS ? SCREEN_H - y : BAND_ROWS;
        if (full) {
            gfx_band_begin_raw(y, rows);   /* HUD, sky and floors cover every pixel */
        } else {
            gfx_band_begin(y, rows, SKY);
        }
        art_world(&s_w, cam, s_frame, esc);
        if (y < HUD_H) {
            art_hud(&s_w, cam, s_total + s_w.score, s_frame);
        }
        if (s_state == ST_PLAY && y < HUD_H + 28) {
            if (push > 0.3f) {
                text_center("PAUSE", HUD_H + 4, 1, WHITE);
                meter((SCREEN_W - 60) / 2, HUD_H + 14, 60, (push - 0.3f) / (PAUSE_PUSH_S - 0.3f), GOLD);
            } else if (s_popup_t > 0 && s_popup) {
                gfx_fill(0, HUD_H + 13, SCREEN_W, 12, PX(40, 8, 8));
                text_center(s_popup, HUD_H + 15, 1, PX(255, 128, 128));
            } else if (h) {
                gfx_fill(0, HUD_H + 13, SCREEN_W, 12, PX(8, 8, 12));
                text_center(h, HUD_H + 15, 1, WHITE);
            }
        }
        draw_overlays();
        display_send_band(y, gfx_band_buf(), rows);
    }
}

static void title_frame(void)
{
    mitem_t items[MENU_MAX];
    int n = menu_items(items);
    char buf[48];
    for (int y = 0; y < SCREEN_H; y += BAND_ROWS) {
        int rows = SCREEN_H - y < BAND_ROWS ? SCREEN_H - y : BAND_ROWS;
        gfx_band_begin(y, rows, SKY);
        for (int i = 0; i < 40; i++) {
            gfx_fill((i * 71) % SCREEN_W, (i * 37) % 120, 1, 1, PX(150, 150, 170));
        }
        disc(298, 54, 9, PX(245, 240, 200));   /* crescent moon */
        disc(303, 50, 8, SKY);
        for (int i = 0; i < 12; i++) {   /* skyline with lit windows */
            int bh = 30 + (i * 37) % 50;
            gfx_fill(i * 28, 200 - bh, 26, bh + 40, PX(27, 34, 64));
            for (int k = 0; k < 6; k++) {
                if ((i + k) % 3) gfx_fill(i * 28 + 4 + (k % 3) * 7, 205 - bh + (k / 3) * 10, 3, 4, PX(232, 216, 112));
            }
        }
        art_balloon(34, 150 + (int)((s_frame / 15) % 4 < 2 ? 0 : 2), s_frame);
        art_thief(262, 238, -1, (s_frame / 8) % 2, 3, false, false);
        text_center("MIDNIGHT HEIST", 14, 3, GOLD);
        text_center("AN ORIGINAL AI PASSPORT CAPER", 40, 1, ORANGE);
        /* the list scrolls: TITLE_ROWS rows around the cursor */
        int first = s_menu_sel - TITLE_ROWS / 2;
        if (first > n - TITLE_ROWS) first = n - TITLE_ROWS;
        if (first < 0) first = 0;
        gfx_blend(40, 54, SCREEN_W - 80, TITLE_ROWS * 13 + 2, PX(8, 10, 24), 228);   /* readable over the skyline */
        for (int i = first; i < n && i < first + TITLE_ROWS; i++) {
            menu_label(&items[i], buf, sizeof(buf));
            int ty = 58 + (i - first) * 13;
            bool locked = items[i].kind == M_LEVEL && items[i].arg > s_unlocked;
            if (i == s_menu_sel) {
                gfx_blend(40, ty - 3, SCREEN_W - 80, 12, WHITE, 40);
                text_shadow(">", 46, ty, 1, GOLD);
            }
            text_shadow(buf, 58, ty, 1, locked ? PX(96, 96, 112) : i == s_menu_sel ? WHITE : GREY);
        }
        if (first > 0) {   /* more above / below */
            for (int k = 0; k < 4; k++) gfx_fill(286 - k, 52 + k, 1 + 2 * k, 1, GOLD);
        }
        if (first + TITLE_ROWS < n) {
            for (int k = 0; k < 4; k++) gfx_fill(286 - k, 58 + TITLE_ROWS * 13 - k, 1 + 2 * k, 1, GOLD);
        }
        snprintf(buf, sizeof(buf), "HI %06d   STARS %d/%d", s_hiscore, total_stars(), 3 * g_level_count);
        text_center(buf, 196, 1, GOLD);
        text_center("UP/DOWN: CHOOSE   OK: SELECT", 208, 1, WHITE);
        if (s_popup_t > 0 && s_popup) {
            text_center(s_popup, 220, 1, GOLD);
        }
        display_send_band(y, gfx_band_buf(), rows);
    }
}

/* ---- power: dim the menus, then switch the panel off on the title -------- */

static bool in_menu(void)
{
    return s_state == ST_TITLE || s_state == ST_PAUSE || s_state == ST_WIN || s_state == ST_LOSE || s_state == ST_OVER;
}

static bool power_update(const input_state_t *in)
{
    if (in->any_pressed && (s_dim || s_screen_off)) {
        if (s_screen_off) {
            display_power(true);
            s_screen_off = false;
        }
        display_set_backlight(100);
        s_dim = false;
        return false;
    }
    if (!in_menu()) {
        return true;
    }
    float idle = input_idle_s();
    if (!s_dim && idle > DIM_AFTER_S) {
        display_set_backlight(DIM_PCT);
        s_dim = true;
    }
    if (!s_screen_off && s_state == ST_TITLE && idle > SCREEN_OFF_AFTER_S) {
        display_power(false);
        s_screen_off = true;
    }
    return true;
}

/* ---- loop ------------------------------------------------------------------- */

static void step(const input_state_t *in, float dt)
{
    if (in->any_pressed || (s_t < 1.0f && s_t + dt >= 1.0f)) {
        s_redraw = true;
    }
    s_t += dt;
    s_frame++;
    switch (s_state) {
    case ST_TITLE:
        if (s_popup_t > 0) {
            s_popup_t -= dt;
        }
        /* fall through */
    case ST_PAUSE:
        update_menu(in);
        break;
    case ST_INTRO:
        if (!in->ok) {
            s_ok_wait_release = false;
        }
        if (s_t >= INTRO_S || (in->ok_pressed && !s_ok_wait_release)) {
            begin_play(in->ok);
        }
        break;
    case ST_PLAY:
        update_play(in, dt);
        break;
    case ST_ESCAPE:
        update_camera(dt, (float)(s_w.exit_col * TILE + 8 - SCREEN_W / 2));
        s_w.clock += dt;
        if (s_t >= ESCAPE_S) {
            finish();
        }
        break;
    case ST_WIN:
    case ST_LOSE:
    case ST_OVER:
        update_end(in);
        break;
    }
}

static void render(void)
{
    bool still = s_state == ST_PAUSE || s_state == ST_WIN || s_state == ST_LOSE || s_state == ST_OVER;
    if (still && !s_redraw) {
        return;   /* nothing moves on these screens: keep the panel as it is */
    }
    if (s_state == ST_TITLE && !s_redraw && (s_frame & 1)) {
        return;   /* a full title frame is ~36 ms of SPI: animate it at 15 fps; a key press redraws at once */
    }
    s_redraw = false;
    int64_t t0 = esp_timer_get_time();
    if (s_state == ST_TITLE) {
        title_frame();
    } else {
        draw_world_frame();
    }
    int64_t us = esp_timer_get_time() - t0;
    s_stat_render_us += us;
    if (us > s_stat_render_max) {
        s_stat_render_max = us;
    }
}

void game_run(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        /* Never erase here: the assistant's settings live in the same NVS. */
        ESP_LOGW(TAG, "nvs init: %s; high score not kept", esp_err_to_name(err));
    } else {
        settings_load();
    }
    sound_init(sound_mode_load());
    s_assistant = boot_assistant_available();
    ESP_LOGI(TAG, "hi score %d, assistant %s, world %u bytes, free heap %u", s_hiscore,
             s_assistant ? "found" : "missing", (unsigned)sizeof(world_t), (unsigned)esp_get_free_heap_size());
    start_level(0, false);   /* a level behind the title, so every pointer is valid */
    to_title(false);

    /* Fixed timestep: the game always advances FRAME_DT per step, so guards,
     * jumps and pellets behave the same however long a frame took. If drawing
     * falls behind, up to MAX_CATCHUP steps run before one draw. */
    int64_t next = esp_timer_get_time();
    for (;;) {
        input_state_t in = input_take();
        if (!power_update(&in)) {
            memset(&in, 0, sizeof(in));
        }
        int64_t now = esp_timer_get_time();
        int steps = 1 + (int)((now - next) / FRAME_US);
        if (steps > MAX_CATCHUP) {
            steps = MAX_CATCHUP;
        }
        for (int i = 0; i < steps; i++) {
            step(&in, FRAME_DT);
            /* edges belong to the first step; later ones see only held keys */
            in.up_pressed = in.down_pressed = in.ok_pressed = false;
            in.up_released = in.down_released = in.ok_released = false;
            in.any_pressed = false;
        }
        if (!s_screen_off) {
            render();
        }
        s_stat_frames++;
        next += s_screen_off ? 100000 : FRAME_US;   /* 30 fps; 10 Hz key polling when off */
        while (esp_timer_get_time() < next) {
            vTaskDelay(1);
        }
        if (esp_timer_get_time() - next > 100000) {
            next = esp_timer_get_time();   /* fell far behind; resync */
            s_stat_late++;
        }
        if (now - s_stat_t0 >= 10000000) {   /* every 10 s: frame rate and heap in the log */
            ESP_LOGI(TAG, "fps %.1f, late %d, render avg %d us max %d us, free heap %u, min %u",
                     s_stat_frames * 1e6 / (double)(now - s_stat_t0), s_stat_late,
                     s_stat_frames ? (int)(s_stat_render_us / s_stat_frames) : 0, (int)s_stat_render_max,
                     (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
            s_stat_t0 = now;
            s_stat_frames = s_stat_late = 0;
            s_stat_render_us = s_stat_render_max = 0;
        }
    }
}
