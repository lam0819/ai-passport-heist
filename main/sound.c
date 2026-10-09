#include "sound.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "sound";

/* AI Passport audio wiring (same as the assistant firmware). */
#define PIN_I2C_SDA 10
#define PIN_I2C_SCL 7
#define PIN_MCLK    6
#define PIN_BCLK    5
#define PIN_WS      3
#define PIN_DOUT    2
#define RATE        16000
#define CHUNK       160    /* frames per render: 10 ms */
#define OUT_VOLUME  70

/* --- tunes ------------------------------------------------------------------
 * Notes are {MIDI note, length in eighths}; 0 is a rest. `chords` gives one
 * bass root per bar of eight eighths, played root-fifth in quarters. */

typedef struct {
    const uint8_t (*notes)[2];
    int count;
    const uint8_t *chords;
    int nchords;
    uint16_t bpm;
    bool loop;
} tune_t;

/* An original sneaky walking-bass loop in D minor (staccato lead over a
 * root-fifth bass); it runs faster when a guard is alerted or time is short. */
static const uint8_t SNEAK[][2] = {
    { 62, 1 }, { 0, 1 }, { 65, 1 }, { 0, 1 }, { 67, 1 }, { 68, 1 }, { 67, 1 }, { 0, 1 },
    { 62, 1 }, { 0, 1 }, { 65, 1 }, { 0, 1 }, { 60, 2 }, { 0, 2 },
    { 62, 1 }, { 0, 1 }, { 65, 1 }, { 0, 1 }, { 67, 1 }, { 70, 1 }, { 69, 1 }, { 67, 1 },
    { 65, 1 }, { 62, 1 }, { 0, 2 }, { 57, 2 }, { 0, 2 },
};
static const uint8_t SNEAK_BASS[] = { 38, 38, 36, 33 };

static const uint8_t CLEAR[][2] = {
    { 72, 1 }, { 76, 1 }, { 79, 1 }, { 84, 2 }, { 0, 1 }, { 79, 1 }, { 84, 6 },
};
static const uint8_t FAIL[][2] = {
    { 67, 2 }, { 64, 2 }, { 60, 2 }, { 55, 6 },
};
static const uint8_t WIN[][2] = {
    { 67, 1 }, { 72, 1 }, { 76, 1 }, { 79, 3 }, { 76, 1 }, { 79, 1 }, { 84, 6 }, { 0, 2 },
    { 81, 1 }, { 79, 1 }, { 77, 1 }, { 76, 1 }, { 74, 1 }, { 76, 1 }, { 72, 8 },
};

#define NOTES(n) n, (int)(sizeof(n) / sizeof(n[0]))
#define LOOP(n, bass, bpm) { NOTES(n), bass, (int)sizeof(bass), bpm, true }
#define JINGLE(n, bpm) { NOTES(n), NULL, 0, bpm, false }
static const tune_t TUNES[MUSIC_COUNT] = {
    [MUSIC_SNEAK] = LOOP(SNEAK, SNEAK_BASS, 128),
    [MUSIC_CLEAR] = JINGLE(CLEAR, 170),
    [MUSIC_FAIL] = JINGLE(FAIL, 120),
    [MUSIC_WIN] = JINGLE(WIN, 150),
};

/* --- sound effects ----------------------------------------------------------
 * A list of steps, each sliding from f0 to f1 Hz; f0 = 0 is a silent gap. */

enum { W_SQ50, W_SQ25, W_SQ12, W_TRI, W_NOISE };

typedef struct {
    uint16_t f0, f1, ms;
    uint8_t wave, vol;
} step_t;

#define S(f0, f1, ms, w, v) { f0, f1, ms, w, v }
static const step_t FX_TICK[] = { S(1800, 1800, 18, W_SQ50, 90) };
static const step_t FX_SELECT[] = { S(880, 880, 60, W_SQ50, 140), S(1320, 1320, 80, W_SQ50, 140) };
static const step_t FX_PAUSE[] = {
    S(988, 988, 60, W_SQ50, 150), S(0, 0, 30, W_SQ50, 0), S(988, 988, 60, W_SQ50, 150),
    S(0, 0, 30, W_SQ50, 0), S(1319, 1319, 130, W_SQ50, 150),
};
static const step_t FX_ZAP[] = { S(1400, 2400, 70, W_SQ25, 110), S(6000, 5000, 40, W_NOISE, 60) };
static const step_t FX_JUMP[] = { S(392, 784, 110, W_SQ50, 110) };
static const step_t FX_LAND[] = { S(140, 110, 40, W_TRI, 200) };
static const step_t FX_BOING[] = { S(180, 720, 240, W_TRI, 240) };
static const step_t FX_SLIP[] = { S(900, 200, 300, W_TRI, 200) };
static const step_t FX_SLAM[] = { S(110, 50, 160, W_TRI, 255), S(400, 200, 90, W_NOISE, 120) };
static const step_t FX_DOOR[] = { S(220, 330, 110, W_TRI, 170) };
static const step_t FX_CLATTER[] = {
    S(1200, 1200, 35, W_SQ12, 90), S(0, 0, 15, W_SQ50, 0), S(1500, 1500, 35, W_SQ12, 90),
    S(0, 0, 15, W_SQ50, 0), S(1800, 1800, 35, W_SQ12, 90),
};
static const step_t FX_ALERT[] = { S(988, 988, 90, W_SQ50, 150), S(1319, 1319, 130, W_SQ50, 150) };
static const step_t FX_DART[] = { S(900, 300, 100, W_SQ25, 90) };
static const step_t FX_BLOCK[] = { S(2000, 2000, 50, W_SQ12, 110) };
static const step_t FX_HIT[] = { S(200, 60, 240, W_SQ25, 160), S(800, 300, 120, W_NOISE, 90) };
static const step_t FX_PICK[] = { S(784, 784, 60, W_SQ50, 140), S(1175, 1175, 90, W_SQ50, 140) };
static const step_t FX_DROP[] = { S(300, 150, 120, W_TRI, 230) };
static const step_t FX_STAIRS[] = { S(330, 520, 140, W_TRI, 240) };
static const step_t FX_LOCKED[] = { S(160, 160, 110, W_SQ50, 110) };
static const step_t FX_ESCAPE[] = {
    S(523, 523, 90, W_SQ50, 150), S(659, 659, 90, W_SQ50, 150), S(784, 784, 90, W_SQ50, 150),
    S(1047, 1047, 180, W_SQ50, 150),
};

#define FX(a) { a, sizeof(a) / sizeof(a[0]) }
static const struct {
    const step_t *steps;
    int count;
} SFX[SFX_COUNT] = {
    [SFX_TICK] = FX(FX_TICK), [SFX_SELECT] = FX(FX_SELECT), [SFX_PAUSE] = FX(FX_PAUSE),
    [SFX_ZAP] = FX(FX_ZAP), [SFX_JUMP] = FX(FX_JUMP), [SFX_LAND] = FX(FX_LAND),
    [SFX_BOING] = FX(FX_BOING), [SFX_SLIP] = FX(FX_SLIP), [SFX_SLAM] = FX(FX_SLAM),
    [SFX_DOOR] = FX(FX_DOOR), [SFX_CLATTER] = FX(FX_CLATTER), [SFX_ALERT] = FX(FX_ALERT),
    [SFX_DART] = FX(FX_DART), [SFX_BLOCK] = FX(FX_BLOCK), [SFX_HIT] = FX(FX_HIT),
    [SFX_PICK] = FX(FX_PICK), [SFX_DROP] = FX(FX_DROP), [SFX_STAIRS] = FX(FX_STAIRS),
    [SFX_LOCKED] = FX(FX_LOCKED), [SFX_ESCAPE] = FX(FX_ESCAPE),
};

/* --- synth state (owned by the task) ---------------------------------------- */

typedef enum { CMD_SFX, CMD_MUSIC, CMD_HURRY, CMD_MODE } cmd_kind_t;
typedef struct {
    uint8_t kind, arg;
} cmd_t;

static QueueHandle_t s_queue;
static esp_codec_dev_handle_t s_spk;
static volatile sound_mode_t s_mode = SOUND_ALL;

static uint32_t s_note_inc[128];   /* phase step per sample for each MIDI note */

static struct {
    const tune_t *tune;
    int idx, bass_eighth;
    uint32_t left, len, eighth, beat_left;
    uint32_t lead_phase, bass_phase;
    uint8_t lead_note, bass_note;
} m;
static bool s_hurry;

/* The C3 has no FPU, so the effects slide in fixed point. */
static struct {
    int id, step;
    uint32_t left, phase;
    int32_t inc, dinc, amp, damp;   /* amp is << 8 */
    uint16_t lfsr;
} fx = { .id = -1, .lfsr = 0xACE1 };

static uint32_t hz_inc(float hz)
{
    return (uint32_t)(hz * 4294967296.0f / RATE);
}

static int square(uint32_t phase, uint32_t duty_hi)
{
    return (phase >> 24) < duty_hi ? 1 : -1;
}

static int triangle(uint32_t phase)
{
    int v = (int)(phase >> 22);   /* 0..1023 */
    return v < 512 ? v * 2 - 512 : 1535 - v * 2;   /* -512..511 */
}

static void music_note(void)
{
    const tune_t *t = m.tune;
    m.lead_note = t->notes[m.idx][0];
    m.len = m.left = t->notes[m.idx][1] * m.eighth;
}

static void music_start(music_t id)
{
    memset(&m, 0, sizeof(m));
    if (id == MUSIC_NONE || !TUNES[id].notes) {
        return;
    }
    m.tune = &TUNES[id];
    uint32_t bpm = m.tune->bpm;
    if (s_hurry && m.tune->loop) {
        bpm = bpm * 5 / 4;
    }
    m.eighth = RATE * 30 / bpm;
    music_note();
}

/* Bass follows the beat, not the melody: root then fifth, a quarter each. */
static void bass_tick(void)
{
    int bar = (m.bass_eighth / 8) % m.tune->nchords;
    int beat = (m.bass_eighth / 2) & 1;
    m.bass_note = m.tune->chords[bar] + (beat ? 7 : 0);
    m.bass_eighth++;
}

static int music_sample(void)
{
    if (!m.tune || s_mode != SOUND_ALL) {
        return 0;
    }
    if (m.tune->nchords && m.beat_left-- == 0) {
        bass_tick();
        m.beat_left = m.eighth - 1;
    }
    int out = 0;
    if (m.lead_note) {
        /* Quick decay to a sustain, with a short gap before the next note. */
        uint32_t played = m.len - m.left;
        if (m.left > m.eighth / 8) {
            int amp = 1700 - (int)(played * 700 / (m.len + 1));
            out += square(m.lead_phase, 64) * amp;
        }
        m.lead_phase += s_note_inc[m.lead_note];
    }
    if (m.tune->nchords) {
        out += triangle(m.bass_phase) * 5;
        m.bass_phase += s_note_inc[m.bass_note];
    }
    if (--m.left == 0) {
        if (++m.idx >= m.tune->count) {
            if (!m.tune->loop) {
                m.tune = NULL;
                return out;
            }
            m.idx = 0;
        }
        music_note();
    }
    return out;
}

static void fx_step(void)
{
    const step_t *s = &SFX[fx.id].steps[fx.step];
    int32_t len = (int32_t)s->ms * RATE / 1000;
    fx.left = (uint32_t)len;
    fx.inc = (int32_t)(hz_inc(s->f0) >> 1);   /* halved so the slide fits int32 */
    fx.dinc = ((int32_t)(hz_inc(s->f1) >> 1) - fx.inc) / len;
    fx.amp = s->vol * 16 << 8;
    fx.damp = fx.amp * 6 / 10 / len;          /* fades to 40 % over the step */
}

static int fx_sample(void)
{
    if (fx.id < 0 || s_mode == SOUND_OFF) {
        return 0;
    }
    const step_t *s = &SFX[fx.id].steps[fx.step];
    int out = 0;
    if (s->f0) {
        int amp = fx.amp >> 8;
        uint32_t prev = fx.phase;
        fx.phase += (uint32_t)fx.inc << 1;
        fx.inc += fx.dinc;
        fx.amp -= fx.damp;
        switch (s->wave) {
        case W_SQ50: out = square(fx.phase, 128) * amp; break;
        case W_SQ25: out = square(fx.phase, 64) * amp; break;
        case W_SQ12: out = square(fx.phase, 32) * amp; break;
        case W_TRI: out = triangle(fx.phase) * amp / 512; break;
        default:
            if (fx.phase < prev) {   /* wrapped: clock the noise */
                fx.lfsr = (fx.lfsr >> 1) ^ (-(fx.lfsr & 1u) & 0xB400u);
            }
            out = (fx.lfsr & 1) ? amp : -amp;
            break;
        }
    }
    if (--fx.left == 0) {
        if (++fx.step >= SFX[fx.id].count) {
            fx.id = -1;
        } else {
            fx_step();
        }
    }
    return out;
}

static void handle(const cmd_t *c)
{
    switch (c->kind) {
    case CMD_SFX:
        fx.id = c->arg;
        fx.step = 0;
        fx_step();
        break;
    case CMD_MUSIC:
        music_start((music_t)c->arg);
        break;
    case CMD_HURRY:
        s_hurry = c->arg;
        if (m.tune && m.tune->loop) {
            music_start((music_t)(m.tune - TUNES));
        }
        break;
    case CMD_MODE:
        s_mode = (sound_mode_t)c->arg;
        break;
    }
}

static void sound_task(void *arg)
{
    (void)arg;
    static int16_t buf[CHUNK * 2];
    for (;;) {
        cmd_t c;
        bool busy = (m.tune && s_mode == SOUND_ALL) || (fx.id >= 0 && s_mode != SOUND_OFF);
        /* Idle: sleep on the queue; the I2S driver plays silence by itself. */
        if (xQueueReceive(s_queue, &c, busy ? 0 : portMAX_DELAY) == pdTRUE) {
            do {
                handle(&c);
            } while (xQueueReceive(s_queue, &c, 0) == pdTRUE);
        }
        for (int i = 0; i < CHUNK; i++) {
            int v = music_sample() + fx_sample();
            if (v > 32767) {
                v = 32767;
            } else if (v < -32768) {
                v = -32768;
            }
            buf[i * 2] = buf[i * 2 + 1] = (int16_t)v;
        }
        esp_codec_dev_write(s_spk, buf, sizeof(buf));
    }
}

/* --- hardware ----------------------------------------------------------------- */

static bool codec_init(void)
{
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        return false;
    }

    i2s_chan_handle_t tx;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = CHUNK;
    if (i2s_new_channel(&chan_cfg, &tx, NULL) != ESP_OK) {
        return false;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_MCLK,
            .bclk = PIN_BCLK,
            .ws = PIN_WS,
            .dout = PIN_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if (i2s_channel_init_std_mode(tx, &std_cfg) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) {
        return false;
    }

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .tx_handle = tx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bus };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!data_if || !ctrl_if || !gpio_if) {
        return false;
    }
    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .use_mclk = true,
        .mclk_div = 256,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *codec = es8311_codec_new(&es_cfg);
    if (!codec) {
        return false;
    }
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data_if };
    s_spk = esp_codec_dev_new(&dev_cfg);
    if (!s_spk) {
        return false;
    }
    esp_codec_dev_sample_info_t fs = { .sample_rate = RATE, .channel = 2, .bits_per_sample = 16 };
    if (esp_codec_dev_open(s_spk, &fs) != ESP_CODEC_DEV_OK) {
        return false;
    }
    esp_codec_dev_set_out_vol(s_spk, OUT_VOLUME);
    return true;
}

void sound_init(sound_mode_t mode)
{
    s_mode = mode < SOUND_MODES ? mode : SOUND_ALL;
    for (int n = 0; n < 128; n++) {
        s_note_inc[n] = hz_inc(440.0f * powf(2.0f, (n - 69) / 12.0f));
    }
    if (!codec_init()) {
        ESP_LOGW(TAG, "no audio codec; playing silently");
        return;
    }
    s_queue = xQueueCreate(8, sizeof(cmd_t));
    xTaskCreate(sound_task, "sound", 3072, NULL, 6, NULL);
}

static void send(cmd_kind_t kind, int arg)
{
    if (s_queue) {
        cmd_t c = { (uint8_t)kind, (uint8_t)arg };
        xQueueSend(s_queue, &c, 0);
    }
}

void sound_play(sfx_t sfx)
{
    send(CMD_SFX, sfx);
}

void sound_music(music_t music)
{
    send(CMD_MUSIC, music);
}

void sound_set_hurry(bool hurry)
{
    send(CMD_HURRY, hurry);
}

void sound_set_mode(sound_mode_t mode)
{
    s_mode = mode;
    send(CMD_MODE, mode);
}

sound_mode_t sound_mode(void)
{
    return s_mode;
}

const char *sound_mode_name(sound_mode_t mode)
{
    static const char *const NAMES[SOUND_MODES] = { "SOUND: ON", "SOUND: FX ONLY", "SOUND: OFF" };
    return mode < SOUND_MODES ? NAMES[mode] : "";
}
