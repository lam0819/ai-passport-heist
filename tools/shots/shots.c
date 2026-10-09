// Host harness: renders README screenshots with the game's own drawing code
// and plays scripted moments that check the rules (the same checks as the
// prototype's headless test).
//
// game.c is compiled in whole (its state is static), the ESP-IDF headers it
// needs come from stub/, and display_send_band() lands in a framebuffer
// instead of the panel. Run tools/shots/make_shots.sh.
#include "../../main/game.c"

#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

static uint16_t s_fb[SCREEN_H * SCREEN_W];
static const char *s_out_dir = ".";
static int s_failures, s_checks;

/* ---- what the board would provide ------------------------------------ */

esp_err_t display_init(void) { return ESP_OK; }
void display_send_band(int y, const uint16_t *buf, int rows)
{
    memcpy(&s_fb[y * SCREEN_W], buf, (size_t)rows * SCREEN_W * sizeof(uint16_t));
}
void display_set_backlight(int pct) { (void)pct; }
void display_power(bool on) { (void)on; }
void input_init(void) {}
input_state_t input_take(void) { input_state_t in = { 0 }; return in; }
float input_idle_s(void) { return 0; }
void sound_init(sound_mode_t mode) { (void)mode; }
void sound_play(sfx_t sfx) { (void)sfx; }
void sound_music(music_t music) { (void)music; }
void sound_set_hurry(bool hurry) { (void)hurry; }
void sound_set_mode(sound_mode_t mode) { (void)mode; }
sound_mode_t sound_mode(void) { return SOUND_ALL; }
const char *sound_mode_name(sound_mode_t mode) { (void)mode; return "SOUND: ON"; }
void boot_mark_valid(void) {}
bool boot_assistant_available(void) { return true; }
void boot_to_assistant(void) {}

/* ---- scripting ---------------------------------------------------------- */

typedef enum { K_NONE, K_LEFT, K_RIGHT } dir_t;

static void check(bool ok, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s  %s\n", ok ? "ok  " : "FAIL", buf);
    s_failures += !ok;
    s_checks++;
}

static bool s_was_ok, s_was_up, s_was_down;

/* Plays frames holding a direction; OK held for the first `ok` frames
 * (the ladder reads one key, so OK hides the direction). */
static void play(dir_t dir, int frames, int ok)
{
    for (int i = 0; i < frames; i++) {
        input_state_t in = { 0 };
        in.ok = i < ok;
        in.up = !in.ok && dir == K_LEFT;
        in.down = !in.ok && dir == K_RIGHT;
        in.ok_pressed = in.ok && !s_was_ok;
        in.ok_released = !in.ok && s_was_ok;
        in.up_pressed = in.up && !s_was_up;
        in.down_pressed = in.down && !s_was_down;
        in.any_pressed = in.ok_pressed || in.up_pressed || in.down_pressed;
        s_was_ok = in.ok;
        s_was_up = in.up;
        s_was_down = in.down;
        step(&in, FRAME_DT);
    }
}

static void idle(int frames) { play(K_NONE, frames, 0); }
static void tap_ok(void) { play(K_NONE, 1, 1); idle(1); }

static void shot(const char *name)
{
    s_redraw = true;
    render();
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.rgb565", s_out_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fwrite(s_fb, sizeof(s_fb), 1, f);
    fclose(f);
}

static int find_level(const char *name)
{
    for (int i = 0; i < g_level_count; i++) if (!strcmp(g_levels[i].name, name)) return i;
    fprintf(stderr, "no level %s\n", name);
    exit(1);
}
static int LV_VAULT, LV_MANSION;

static void go_level(int i)
{
    s_total = 0;
    s_lives = LIVES;
    start_level(i, false);
    begin_play(false);
    s_was_ok = s_was_up = s_was_down = false;
}

static guard_t *find_guard(int type, int f, int state)
{
    for (int i = 0; i < s_w.nguards; i++) {
        guard_t *g = &s_w.guards[i];
        if ((type < 0 || g->type == type) && (f < 0 || g->f == f) && (state < 0 || g->state == state)) return g;
    }
    return NULL;
}

static void put(int f, float x)
{
    s_w.p.f = (uint8_t)f;
    s_w.p.x = x;
    s_w.p.vx = 0;
    s_w.p.y = 0;
    s_w.p.air = false;
}

static void freeze(guard_t *g, float x, int face)
{
    g->x = x;
    g->face = (int8_t)face;
    g->wait = 99;
    g->state = G_PATROL;
    g->spot = 0;
}

static float peak_jump(int run_frames, int hold_frames, float *dist)
{
    go_level(0);
    put(1, 20);
    if (run_frames) play(K_RIGHT, run_frames, 0);
    float x0 = s_w.p.x, peak = 0;
    play(K_NONE, 1, hold_frames ? hold_frames : 1);
    for (int i = 1; i < 60 && (s_w.p.air || i < 8); i++) {
        play(K_NONE, 1, i < hold_frames ? 1 : 0);
        if (s_w.p.y > peak) peak = s_w.p.y;
    }
    idle(1);
    if (dist) *dist = s_w.p.x - x0;
    return peak;
}

/* ---- autopilot: plays a level with the real controls ------------------------
 * Proves every map can be finished in the engine itself, not just on paper:
 * walking, opening doors, running jumps over crates, stairs, springboards,
 * picking up every item and escaping (bonus: every gold bar). Guards are
 * stunned and spotlights off -- this checks the building, the stealth is
 * the player's job. Route choice: nearest remaining item by route cost. */

#define BOT_INF 1e9f
static float s_bdist[MAX_FLOORS][MAX_COLS];

static float bot_door_x(const door_t *d) { return (float)(d->col * TILE + 8); }
static char bot_tile(int f, int c) { return s_w.L->rows[f][c]; }

static float bot_step_cost(int f, int c)   /* entering column c on floor f */
{
    char ch = bot_tile(f, c);
    return 1.0f + (ch == '|' ? 2.6f : ch == 'x' || ch == 'b' ? 1.3f : ch == 'p' ? 0.9f : 0.0f);
}

/* route cost from every cell to (tf, tc): Bellman-Ford on <= 4 x 64 cells */
static void bot_costs(int tf, int tc)
{
    for (int f = 0; f < s_w.nf; f++) for (int c = 0; c < s_w.w; c++) s_bdist[f][c] = BOT_INF;
    s_bdist[tf][tc] = 0;
    for (bool changed = true; changed;) {
        changed = false;
        for (int f = 0; f < s_w.nf; f++) {
            for (int c = 0; c < s_w.w; c++) {
                float best = s_bdist[f][c];
                if (c > 0 && s_bdist[f][c - 1] + bot_step_cost(f, c - 1) < best) best = s_bdist[f][c - 1] + bot_step_cost(f, c - 1);
                if (c + 1 < s_w.w && s_bdist[f][c + 1] + bot_step_cost(f, c + 1) < best) best = s_bdist[f][c + 1] + bot_step_cost(f, c + 1);
                char ch = bot_tile(f, c);
                if (ch == '^' && s_bdist[f - 1][c] + 4 < best) best = s_bdist[f - 1][c] + 4;
                if (ch == 'v' && s_bdist[f + 1][c] + 4 < best) best = s_bdist[f + 1][c] + 4;
                if (ch == 'b' && s_bdist[f - 1][c] + 4.4f < best) best = s_bdist[f - 1][c] + 4.4f;
                if (best < s_bdist[f][c]) { s_bdist[f][c] = best; changed = true; }
            }
        }
    }
}

static int s_bot_frames;
static void bot_frame(int dir, bool ok)
{
    play(dir < 0 ? K_LEFT : dir > 0 ? K_RIGHT : K_NONE, 1, ok ? 1 : 0);
    s_bot_frames++;
    for (int i = 0; i < s_w.nguards; i++) {   /* keep everyone asleep on their feet */
        s_w.guards[i].state = G_STUNNED;
        s_w.guards[i].stun = 999;
    }
    s_w.nlights = 0;
    s_w.ndarts = 0;
}

static void bot_settle(void)
{
    for (int i = 0; i < 40 && (fabsf(s_w.p.vx) > 0.01f || s_w.p.air || s_w.p.slip > 0) && s_state == ST_PLAY; i++) bot_frame(0, false);
}

static void bot_tap(void)
{
    bot_frame(0, true);
    bot_frame(0, false);
}

/* Walk along this floor until the thief's centre is near column c. */
static bool bot_walk_to(int c, int tol)
{
    float goal = (float)(c * TILE + 8);
    int f = s_w.p.f, stuck = 0;
    float last = s_w.p.x;
    for (int guard = 0; guard < 30 * 60 && s_state == ST_PLAY && s_w.p.f == f; guard++) {
        float cx = thief_cx(&s_w), err = goal - cx;
        if (fabsf(err) < tol && !s_w.p.air) {
            bot_settle();
            if (fabsf(goal - thief_cx(&s_w)) < tol) return true;
            continue;
        }
        int dir = err > 0 ? 1 : -1;
        /* a closed door ahead: stop and open it */
        const door_t *d = world_door_ahead(&s_w);
        if (d && !d->open && (bot_door_x(d) - cx) * dir > 0 && fabsf(err) > fabsf(bot_door_x(d) - cx)) {
            bot_settle();
            if (s_w.p.face != dir) bot_frame(dir, false);
            bot_settle();
            bot_tap();
            continue;
        }
        /* a crate ahead: run-up jump over it (back off first if too close) */
        bool jumped = false;
        for (int i = 0; i < s_w.ncrates && !s_w.p.air; i++) {
            const cell_t *k = &s_w.crates[i];
            if (k->f != f || s_w.p.y > 0) continue;
            float edge = dir > 0 ? (float)(k->col * TILE + 1) - (s_w.p.x + BODY_W) : s_w.p.x - (float)(k->col * TILE + TILE - 1);
            float kc = (float)(k->col * TILE + 8);
            if ((kc - cx) * dir <= 0 || fabsf(err) < fabsf(kc - cx)) continue;
            if (edge <= 26 && edge >= 8 && fabsf(s_w.p.vx) >= 45) {
                bot_frame(0, true);
                for (int h = 0; h < 13; h++) bot_frame(0, true);
                for (int h = 0; h < 30 && s_w.p.air; h++) bot_frame(dir, false);
                jumped = true;
            } else if (edge < 8 && fabsf(s_w.p.vx) < 45) {
                for (int h = 0; h < 16; h++) bot_frame(-dir, false);
                bot_settle();
                jumped = true;
            }
            break;
        }
        /* a springboard on the way that is not the goal: jump over it, or it bounces you upstairs */
        for (int i = 0; i < s_w.nsprings && !jumped && !s_w.p.air; i++) {
            const spring_t *b = &s_w.springs[i];
            float kc = (float)(b->col * TILE + 8), dk = (kc - cx) * dir;
            if (b->f != f || dk <= 0 || fabsf(err) <= dk + 1) continue;
            if (dk <= 30 && dk >= 14 && fabsf(s_w.p.vx) >= 45) {
                for (int h = 0; h < 14; h++) bot_frame(0, true);
                for (int h = 0; h < 30 && s_w.p.air; h++) bot_frame(dir, false);
                jumped = true;
            } else if (dk < 14) {
                for (int h = 0; h < 18; h++) bot_frame(-dir, false);
                bot_settle();
                jumped = true;
            }
        }
        if (jumped) continue;
        /* near the goal: let go early, the glide covers the rest */
        bool brake = fabsf(err) < 6 && fabsf(s_w.p.vx) > 20;
        bot_frame(brake ? 0 : dir, false);
        if (s_state == ST_PAUSE) bot_tap();   /* pushed a wall too long: resume */
        stuck = fabsf(s_w.p.x - last) < 0.01f ? stuck + 1 : 0;
        last = s_w.p.x;
        if (stuck > 90) return false;
    }
    return s_state != ST_PLAY || s_w.p.f != f;
}

/* One leg: from where the thief is to cell (tf, tc). */
static bool bot_go(int tf, int tc)
{
    bot_costs(tf, tc);
    for (int hops = 0; hops < 24 && s_state == ST_PLAY; hops++) {
        int f = s_w.p.f, c = (int)(thief_cx(&s_w) / TILE);
        if (f == tf) {
            /* same floor: walk unless a detour via other floors is cheaper (never needed here) */
            return bot_walk_to(tc, 5);
        }
        /* best transition on this floor */
        int best_c = -1;
        float best = BOT_INF;
        for (int k = 0; k < s_w.w; k++) {
            char ch = bot_tile(f, k);
            float tcost;
            int nf;
            if (ch == '^') { nf = f - 1; tcost = 4; }
            else if (ch == 'v') { nf = f + 1; tcost = 4; }
            else if (ch == 'b') { nf = f - 1; tcost = 4.4f; }
            else continue;
            float walk = 0;
            for (int x = c; x != k; x += k > c ? 1 : -1) walk += bot_step_cost(f, x + (k > c ? 1 : -1));
            float total = walk + tcost + s_bdist[nf][k];
            if (total < best) { best = total; best_c = k; }
        }
        if (best_c < 0 || best >= BOT_INF) return false;
        char ch = bot_tile(f, best_c);
        if (ch == 'b') {   /* walk over the springboard: it does the rest */
            bot_walk_to(best_c, 3);
            for (int i = 0; i < 90 && (s_w.p.air || s_w.p.f == f) && s_state == ST_PLAY; i++) bot_frame(0, false);
        } else {
            if (!bot_walk_to(best_c, 6)) return false;
            bot_tap();
            for (int i = 0; i < 12; i++) bot_frame(0, false);
        }
        if (s_w.p.f == f) return false;
    }
    return s_w.p.f == tf;
}

/* Plays level i to the end; returns seconds on the clock used, < 0 on failure. */
static float bot_play(int i, char *why, size_t n)
{
    go_level(i);
    s_bot_frames = 0;
    for (int guard = 0; guard < 32 && s_state == ST_PLAY; guard++) {
        int best = -1;
        float bd = BOT_INF;
        for (int k = 0; k < s_w.nloot; k++) {
            const loot_t *l = &s_w.loot[k];
            if (l->state != LS_FLOOR) continue;
            /* exact cost from here to the item: */
            bot_costs(l->f, (int)((l->x + 5) / TILE));
            float d = s_bdist[s_w.p.f][(int)(thief_cx(&s_w) / TILE)];
            if (d < bd) { bd = d; best = k; }
        }
        if (best < 0) break;
        const loot_t *l = &s_w.loot[best];
        if (!bot_go(l->f, (int)((l->x + 5) / TILE))) {
            snprintf(why, n, "stuck on the way to item %d (floor %d col %d), thief floor %d x %.0f", best, l->f, (int)((l->x + 5) / TILE), s_w.p.f, s_w.p.x);
            return -1;
        }
        for (int k = 0; k < 4 && s_state == ST_PLAY; k++) bot_frame(0, false);
        if (s_w.loot[best].state != LS_CARRIED && s_state == ST_PLAY) {
            snprintf(why, n, "at item %d but did not pick it up", best);
            return -1;
        }
    }
    if (!s_w.L->bonus && s_state == ST_PLAY) {
        if (!bot_go(0, s_w.exit_col)) {
            snprintf(why, n, "stuck on the way to the exit");
            return -1;
        }
        bot_settle();
        bot_tap();
        for (int k = 0; k < 30 * 4 && s_state == ST_ESCAPE; k++) idle(1);
    }
    if (s_state != ST_WIN) {
        snprintf(why, n, "ended in state %d (time left %.1f)", s_state, s_w.time_left);
        return -1;
    }
    return s_bot_frames * FRAME_DT;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        s_out_dir = argv[1];
    }
    stub_nvs_wipe();
    settings_load();   /* fresh NVS: progress starts at 1-1 */
    LV_VAULT = find_level("BONUS: VAULT");
    LV_MANSION = find_level("2-3 MANSION");
    printf("sizeof: world %zu bytes (guard %zu, thief %zu), band buffers %zu bytes\n",
           sizeof(world_t), sizeof(guard_t), sizeof(thief_t), sizeof(uint16_t) * 2 * SCREEN_W * BAND_ROWS);
    start_level(0, false);
    to_title(false);
    for (int i = 0; i < 12; i++) step(&(input_state_t){ 0 }, FRAME_DT);
    /* the title screenshot is taken below, with the progress checks */
    check(s_state == ST_TITLE, "title renders");

    /* 1-1: intro, walk */
    s_total = 0;
    start_level(0, false);
    shot("intro");
    begin_play(false);
    float x0 = s_w.p.x;
    play(K_RIGHT, 30, 0);
    idle(10);
    check(s_w.p.x > x0 + 40, "hold DOWN walks right (%.0f px)", s_w.p.x - x0);

    /* stun gun: standing tap shoots, the pellet stuns */
    go_level(0);
    guard_t *g = find_guard(GT_GUARD, 1, G_PATROL);
    freeze(g, g->x, 1);
    put(1, g->x - 50);
    s_w.p.face = 1;
    tap_ok();
    check(!s_w.p.air && s_w.nbolts == 1, "standing tap OK fires the stun gun (no jump)");
    idle(3);
    shot("shoot");
    idle(12);
    check(g->state == G_STUNNED, "the pellet stuns the guard ahead");
    shot("stunned");
    idle(30 * 6);
    check(g->state != G_STUNNED, "stun wears off after ~5 s");

    /* shield guard */
    go_level(LV_MANSION);
    g = find_guard(GT_SHIELD, -1, -1);
    freeze(g, g->x, -1);
    put(g->f, g->x - 50);
    s_w.p.face = 1;
    tap_ok();
    idle(15);
    check(g->state == G_ALERT, "shield guard: a pellet from the front is blocked and alerts him");
    go_level(LV_MANSION);
    g = find_guard(GT_SHIELD, -1, -1);
    freeze(g, g->x, 1);
    put(g->f, g->x - 50);
    s_w.p.face = 1;
    tap_ok();
    idle(15);
    check(g->state == G_STUNNED, "shield guard: a pellet from behind stuns");

    /* door slam */
    go_level(0);
    door_t *d = NULL;
    for (int i = 0; i < s_w.ndoors; i++) if (s_w.doors[i].f == 1) d = &s_w.doors[i];
    float dx = (float)(d->col * TILE + 8);
    g = find_guard(GT_GUARD, 1, G_PATROL);
    freeze(g, dx + 4, 1);
    put(1, dx - 16);
    s_w.p.face = 1;
    int s0 = s_w.score;
    tap_ok();
    check(d->open, "tap OK at a door opens it");
    check(g->state == G_STUNNED && s_w.score > s0, "the door slams the guard behind it (+%d)", s_w.score - s0);
    idle(4);
    shot("slam");

    /* closed door blocks */
    go_level(0);
    put(1, (float)(d->col * TILE - 20));
    play(K_RIGHT, 30, 0);
    check(s_w.p.x < d->col * TILE + 8, "a closed door blocks the way");

    /* hide */
    go_level(0);
    cell_t *h = NULL;
    for (int i = 0; i < s_w.nhides; i++) if (s_w.hides[i].f == 1 && !h) h = &s_w.hides[i];
    g = find_guard(GT_GUARD, 1, G_PATROL);
    put(1, (float)(h->col * TILE + 2));
    freeze(g, s_w.p.x + 60, -1);
    play(K_NONE, 20, 21);
    check(s_w.p.hidden, "hold OK at an alcove hides");
    check(g->spot < 0.05f && g->state != G_ALERT, "hidden: a guard looking straight at you sees nothing");
    shot("hide");
    idle(1);
    check(!s_w.p.hidden, "release OK steps out");
    idle(20);
    check(g->spot > 0 || g->state == G_ALERT, "out of hiding in front of him: spotted");

    /* loot, darts, drop */
    go_level(0);
    g = find_guard(GT_GUARD, 1, G_PATROL);
    loot_t *loot = NULL;
    for (int i = 0; i < s_w.nloot; i++) if (s_w.loot[i].f == 1) loot = &s_w.loot[i];
    put(1, loot->x - 2);
    idle(2);
    check(loot->state == LS_CARRIED, "walking over loot picks it up");
    freeze(g, s_w.p.x + 70, -1);
    int lives0 = s_w.lives;
    for (int i = 0; i < 30 * 4 && s_w.lives == lives0; i++) {
        idle(1);
        if (i == 40) shot("alert");
    }
    check(s_w.lives == lives0 - 1, "an alerted guard shoots: life lost");
    check(loot->state == LS_FLOOR, "the hit drops the carried loot");

    /* drop only ONE */
    go_level(LV_MANSION);
    for (int i = 0; i < s_w.nguards; i++) { s_w.guards[i].state = G_STUNNED; s_w.guards[i].stun = 999; }
    for (int i = 0; i < s_w.nloot; i++) {
        if (s_w.loot[i].f == 1) { put(1, s_w.loot[i].x - 2); idle(2); }
    }
    int n0 = s_w.p.ncarry, last = s_w.p.carry[n0 - 1];
    g = find_guard(GT_DOG, -1, -1);
    g->state = G_ALERT;
    put(g->f, g->x);
    idle(3);
    check(n0 == 2 && s_w.p.ncarry == 1 && s_w.loot[last].state == LS_FLOOR,
          "caught carrying 2: drops only the last one (%d -> %d)", n0, s_w.p.ncarry);

    /* jumping */
    go_level(0);
    put(1, 40);
    play(K_RIGHT, 12, 0);
    play(K_NONE, 1, 1);
    check(s_w.p.air && s_w.p.vy > 0, "moving + press OK jumps at once");
    float d_run, d_stand;
    float p_tap = peak_jump(12, 1, NULL), p_hold = peak_jump(12, 20, NULL);
    check(p_hold > p_tap + 3, "hold OK longer = higher (%.0f vs %.0f px)", p_tap, p_hold);
    peak_jump(20, 20, &d_run);
    go_level(0);
    put(1, 40);
    play(K_NONE, 30, 30);
    d_stand = s_w.p.x - 40;
    idle(30);
    check(d_run > d_stand + 20, "run-up jumps farther (%.0f vs standing %.0f px)", d_run, d_stand);
    go_level(0);
    put(1, 40);
    play(K_NONE, 4, 9);
    check(!s_w.p.air, "standing: the first 0.13 s of a hold is still a tap");
    play(K_NONE, 4, 4);
    check(s_w.p.air, "standing: holding OK past 0.18 s jumps");
    check(CEIL > DART_H && CEIL + 37 <= FEET_DY, "jump peak (%d px) clears darts (%d) under the ceiling", (int)CEIL, DART_H);

    /* crates */
    go_level(0);
    cell_t *cr = &s_w.crates[0];
    put(cr->f, (float)(cr->col * TILE - 40));
    play(K_RIGHT, 45, 0);
    idle(10);
    check(s_w.p.x < cr->col * TILE, "a crate blocks walking");
    go_level(0);
    put(cr->f, (float)(cr->col * TILE - 44));
    play(K_RIGHT, 14, 0);
    play(K_NONE, 9, 14);
    shot("jump");
    play(K_NONE, 5, 0);
    play(K_RIGHT, 20, 0);
    idle(15);
    check(s_w.p.x > cr->col * TILE + 16, "a running jump clears the crate (x %.0f)", s_w.p.x);
    go_level(0);
    put(cr->f, (float)(cr->col * TILE + 2));
    s_w.p.y = 30;
    s_w.p.air = true;
    idle(20);
    check(s_w.p.y == CRATE_H && !s_w.p.air, "landing on a crate stands on top");

    /* springboard */
    go_level(0);
    spring_t *sb = &s_w.springs[0];
    put(sb->f, (float)(sb->col * TILE - 24));
    play(K_RIGHT, 10, 0);
    idle(6);
    shot("spring");
    for (int i = 0; i < 60 && (s_w.p.air || s_w.p.f == sb->f); i++) idle(1);
    check(s_w.p.f == sb->f - 1 && !s_w.p.air, "the springboard bounces you up through the hatch (floor %d)", s_w.p.f);

    /* banana */
    go_level(LV_MANSION);
    peel_t *pe = NULL;
    for (int i = 0; i < s_w.npeels; i++) if (s_w.peels[i].f == 1) pe = &s_w.peels[i];
    g = find_guard(GT_GUARD, 1, G_PATROL);
    freeze(g, pe->x + 70, 1);
    put(1, pe->x - 36);
    play(K_RIGHT, 25, 0);
    check(s_w.p.slip > 0 || s_w.p.x > pe->x + 4, "walking onto a banana peel: slip");
    shot("peel");
    check(g->state == G_INVESTIGATE || g->state == G_ALERT, "the slip's noise draws a guard");
    go_level(LV_MANSION);
    put(1, pe->x - 40);
    play(K_RIGHT, 14, 0);
    play(K_NONE, 14, 14);
    play(K_RIGHT, 10, 0);
    idle(10);
    check(s_w.p.x > pe->x && s_w.p.slip <= 0, "jumping the peel: no slip");

    /* stairs, pause, sleeper, can, dog, time */
    go_level(0);
    const stairs_t *up = NULL;
    for (int i = 0; i < s_w.nstairs; i++) if (s_w.stairs[i].dir < 0) up = &s_w.stairs[i];
    put(up->f, (float)(up->col * TILE + 2));
    tap_ok();
    check(s_w.p.f == up->f - 1, "tap OK on stairs goes up");
    go_level(0);
    put(1, 2);
    play(K_LEFT, (int)(PAUSE_PUSH_S * 30) + 5, 0);
    check(s_state == ST_PAUSE, "pushing into the wall for 1.5 s pauses");
    shot("pause");
    tap_ok();
    check(s_state == ST_PLAY, "OK on RESUME");
    go_level(0);
    g = find_guard(GT_GUARD, -1, G_SLEEP);
    put(g->f, g->x + 8);
    idle(60);
    check(g->state != G_SLEEP, "lingering next to a sleeper wakes him");
    go_level(LV_MANSION);
    can_t *can = NULL;
    for (int i = 0; i < s_w.ncans; i++) if (s_w.cans[i].f == 1) can = &s_w.cans[i];
    g = find_guard(GT_GUARD, 1, G_PATROL);
    freeze(g, can->x + 60, 1);
    put(1, can->x - 30);
    play(K_RIGHT, 20, 0);
    check(g->state == G_INVESTIGATE || g->state == G_ALERT, "kicking a can draws a guard");
    go_level(LV_MANSION);
    g = find_guard(GT_DOG, -1, -1);
    freeze(g, g->x, -1);
    put(g->f, g->x - 40);
    lives0 = s_w.lives;
    idle(90);
    check(s_w.lives < lives0, "the guard dog charges and bites");
    go_level(LV_MANSION);
    s_w.time_left = 0.05f;
    idle(3);
    check(s_state == ST_LOSE && s_w.lives == LIVES - 1, "time up costs a life");
    shot("busted");

    /* full run of 1-1 */
    go_level(0);
    for (int i = 0; i < s_w.nguards; i++) { s_w.guards[i].state = G_STUNNED; s_w.guards[i].stun = 999; }
    for (int i = 0; i < s_w.nloot; i++) { put(s_w.loot[i].f, s_w.loot[i].x - 2); idle(2); }
    check(world_loot_left(&s_w) == 0, "all loot carried");
    put(0, (float)(s_w.exit_col * TILE + 2));
    tap_ok();
    check(s_state == ST_ESCAPE, "tap OK at the EXIT with all loot: escape by balloon");
    idle(30);
    shot("escape");
    idle(60);
    check(s_state == ST_WIN && s_result.stars >= 1, "1-1 escaped: %d stars, level %d", s_result.stars, s_result.total);
    shot("clear");
    go_level(0);
    put(0, (float)(s_w.exit_col * TILE + 2));
    tap_ok();
    check(s_state == ST_PLAY, "the exit stays locked until all loot is taken");

    /* bonus */
    go_level(LV_VAULT);
    idle(20);
    shot("bonus");
    s_w.nlights = 0;
    for (int i = 0; i < s_w.nloot && s_state == ST_PLAY; i++) { put(s_w.loot[i].f, s_w.loot[i].x - 2); idle(1); }
    check(s_state == ST_WIN && s_result.all, "bonus: all the gold (+%d)", s_result.total);
    go_level(LV_VAULT);
    light_t *L = &s_w.lights[0];
    put(L->f, L->x + L->w / 2 - 6);
    idle(2);
    check(s_state == ST_WIN && !s_result.all, "bonus: a spotlight ends it");

    /* game over */
    go_level(LV_MANSION);
    for (int k = 0; k < 3; k++) {
        g = find_guard(GT_DOG, -1, -1);
        s_w.p.inv = s_w.p.knock = 0;
        put(g->f, g->x);
        g->state = G_ALERT;
        idle(10);
    }
    check(s_state == ST_OVER, "3 hits: game over");
    shot("over");

    /* unattended 60 s on 1-2 */
    go_level(LV_MANSION);
    put(2, 2);
    for (int i = 0; i < 60 * 30 && s_state == ST_PLAY; i++) idle(1);
    check(true, "60 s of patrols without errors (state %d)", s_state);


    /* ---- every level, played to the end by the autopilot ------------------- */
    printf("autopilot (guards stunned, spotlights off; real controls):\n");
    for (int i = 0; i < g_level_count; i++) {
        char why[160] = "";
        go_level(i);
        check(s_w.time_left == g_levels[i].time && s_w.nf == g_levels[i].floors, "%-13s loads: %d floors, %d cols, %d items, %d guards, %d s",
              g_levels[i].name, s_w.nf, s_w.w, s_w.nloot, s_w.nguards, g_levels[i].time);
        float used = bot_play(i, why, sizeof(why));
        check(used > 0 && used < g_levels[i].time, "%-13s finished by the autopilot in %.1f s of %d s%s%s",
              g_levels[i].name, used, g_levels[i].time, why[0] ? ": " : "", why);
        if (i == find_level("4-3 TOWER") && used > 0) shot("tower_clear");
    }

    /* ---- tall buildings: the view follows the thief up and down ------------ */
    {
        int t = find_level("4-3 TOWER");
        go_level(t);
        check(s_w.nf == 4 && s_w.y0 == SCREEN_H - 4 * FLOOR_H, "4 floors: the ground floor starts on the bottom edge (y0 %d)", s_w.y0);
        for (int i = 0; i < s_w.nguards; i++) { s_w.guards[i].state = G_STUNNED; s_w.guards[i].stun = 999; }
        put(1, 300);
        idle(45);
        int fy = (int)feet_y(&s_w, 1);
        check(s_w.y0 == SCREEN_H - 3 * FLOOR_H && fy > HUD_H + 40 && fy <= SCREEN_H, "upstairs the view glides up to the roof (y0 %d, feet at %d)", s_w.y0, fy);
        shot("tower");
        put(3, 300);
        idle(45);
        fy = (int)feet_y(&s_w, 3);
        check(s_w.y0 == SCREEN_H - 4 * FLOOR_H && fy <= SCREEN_H && fy > HUD_H, "back on the ground floor the view follows (feet at %d)", fy);
        int n0 = s_w.nfx;
        s_w.p.f = 3;
        tap_ok();   /* a pop-up text sits on its floor whatever the view */
        check(s_w.nfx >= n0, "pop-up texts are stored per floor, not per screen row");
    }

    /* ---- progress: strictly in order, saved, survives a reboot, resettable -- */
    {
        stub_nvs_wipe();
        uint8_t old = 3;
        nvs_set_u8(1, "stars2", old);   /* a star key from the old three-level layout */
        settings_load();
        check(s_unlocked == 0 && total_stars() == 0, "first run: only 1-1 is open, no stars");
        check(stub_nvs_find("stars2", 0) == NULL, "the old layout's star keys are dropped");
        to_title(false);
        for (int i = 0; i < 4; i++) step(&(input_state_t){ 0 }, FRAME_DT);
        shot("title");
        mitem_t items[MENU_MAX];
        int n = menu_items(items);
        check(items[1].kind == M_LEVEL && items[1].arg == 1, "the title lists every level (%d menu rows)", n);
        s_menu_sel = 1;
        menu_choose(&items[1]);
        check(s_state == ST_TITLE && s_popup_t > 0, "a locked level does not start: %s", s_popup ? s_popup : "");
        char label[48];
        menu_label(&items[1], label, sizeof(label));
        check(strstr(label, "LOCKED") != NULL, "locked levels say so: \"%s\"", label);
        /* play 1-1 to the end for real: the next level opens */
        char why[160] = "";
        float used = bot_play(0, why, sizeof(why));
        check(used > 0 && s_unlocked == 1 && s_stars[0] >= 1, "clearing 1-1 opens 1-2 (stars %d)", s_stars[0]);
        play(K_NONE, 40, 0);
        tap_ok();
        check(s_state == ST_INTRO && s_level == 1, "OK on the result screen goes on to 1-2");
        /* a reboot: statics forgotten, NVS kept */
        s_unlocked = 0;
        memset(s_stars, 0, sizeof(s_stars));
        settings_load();
        check(s_unlocked == 1 && s_stars[0] >= 1, "after a reboot the progress is back (open up to %d, 1-1 stars %d)", s_unlocked + 1, s_stars[0]);
        /* finishing each level in order opens the next, all the way */
        for (int i = 1; i < g_level_count; i++) {
            go_level(i);
            s_w.time_left = 1;
            for (int k = 0; k < s_w.nloot; k++) { s_w.loot[k].state = LS_CARRIED; s_w.p.carry[s_w.p.ncarry++] = (uint8_t)k; }
            if (s_w.L->bonus) idle(1); else finish();
        }
        check(s_unlocked == g_level_count - 1, "clearing every level in order opens all %d", g_level_count);
        check(s_stars[3] == 3, "a bonus stage with all the gold earns 3 stars");
        to_title(false);
        check(s_menu_sel == g_level_count - 1, "the title cursor waits on the furthest level");
        s_menu_sel = g_level_count + 2;   /* scrolled down to the settings */
        for (int i = 0; i < 4; i++) step(&(input_state_t){ 0 }, FRAME_DT);
        shot("levels");
        n = menu_items(items);
        int r = -1;
        for (int i = 0; i < n; i++) if (items[i].kind == M_RESET) r = i;
        s_menu_sel = r;
        menu_choose(&items[r]);
        check(s_unlocked == g_level_count - 1 && s_reset_armed, "RESET PROGRESS asks first (one OK changes nothing)");
        menu_choose(&items[r]);
        check(s_unlocked == 0 && total_stars() == 0, "a second OK resets the progress");
        s_unlocked = 5;
        settings_load();
        check(s_unlocked == 0, "the reset is saved");
        s_menu_sel = r;
        menu_choose(&items[r]);
        s_menu_sel = 0;
        update_menu(&(input_state_t){ .down_pressed = true, .any_pressed = true });
        check(!s_reset_armed, "moving the cursor cancels a pending reset");
        stub_nvs_wipe();
        settings_load();
    }

    /* cost estimate: pixels written and host time for a busy play frame */
    go_level(LV_MANSION);
    put(1, 300);
    for (int i = 0; i < s_w.nguards; i++) s_w.guards[i].state = G_ALERT;
    idle(5);
    extern long g_gfx_px;
    g_gfx_px = 0;
    s_redraw = true;
    render();
    long px = g_gfx_px;
    // Host timing is informational. Some Macs have a stray FFmpeg <time.h>
    // in /usr/local/include that hides the C library one: then skip it.
#ifdef CLOCKS_PER_SEC
    clock_t c0 = clock();
    for (int i = 0; i < 200; i++) { s_redraw = true; render(); }
    double r_us = (double)(clock() - c0) / CLOCKS_PER_SEC * 1e6 / 200;
    c0 = clock();
    for (int i = 0; i < 2000; i++) { winput_t wi = { 0 }; world_update(&s_w, &wi, FRAME_DT); }
    double u_us = (double)(clock() - c0) / CLOCKS_PER_SEC * 1e6 / 2000;
#else
    double r_us = 0, u_us = 0;
#endif
    printf("busy frame (2-3 mansion, %d guards): %ld pixel writes; host render %.0f us, host update %.1f us\n",
           s_w.nguards, px, r_us, u_us);
    go_level(find_level("4-3 TOWER"));
    put(0, 400);
    for (int i = 0; i < s_w.nguards; i++) s_w.guards[i].state = G_ALERT;
    idle(30);
    g_gfx_px = 0;
    s_redraw = true;
    render();
    printf("busy frame (4-3 tower, 4 floors, %d guards): %ld pixel writes\n", s_w.nguards, g_gfx_px);
    go_level(LV_VAULT);
    g_gfx_px = 0;
    s_redraw = true;
    render();
    printf("bonus frame (3 spotlights): %ld pixel writes\n", g_gfx_px);
    printf("%d checks, %s\n", s_checks, s_failures ? "SOME CHECKS FAILED" : "all checks passed");
    return s_failures ? 1 : 0;
}
