#include "input.h"

#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "input";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static input_state_t s_state;
static bool s_pressed[3], s_released[3];   /* up, down, ok: latched until the game reads */
static volatile int64_t s_last_change_us;

typedef enum { K_NONE = 0, K_UP, K_DOWN, K_OK } key_t;

static key_t read_key(int *mv_out)
{
    if (!s_adc || !s_cali) {
        return K_NONE;
    }
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, BOARD_KEYS_ADC_CHAN, &raw) != ESP_OK) {
            return K_NONE;
        }
        sum += raw;
    }
    int mv = 0;
    if (adc_cali_raw_to_voltage(s_cali, sum / 8, &mv) != ESP_OK) {
        return K_NONE;
    }
    if (mv_out) {
        *mv_out = mv;
    }
    if (mv < BOARD_KEY_UP_MAX_MV) {
        return K_UP;
    }
    if (mv < BOARD_KEY_DOWN_MAX_MV) {
        return K_DOWN;
    }
    if (mv < BOARD_KEY_OK_MAX_MV) {
        return K_OK;
    }
    return K_NONE;
}

static void latch(bool now, bool *held, int i)
{
    if (now && !*held) {
        s_pressed[i] = true;
    } else if (!now && *held) {
        s_released[i] = true;
    }
    *held = now;
}

static void input_task(void *arg)
{
    (void)arg;
    key_t stable = K_NONE;
    key_t candidate = K_NONE;
    int confirm = 0;
    for (;;) {
        int mv = 0;
        key_t now = read_key(&mv);
        if (now == candidate) {
            if (confirm < 3) {
                confirm++;
            }
        } else {
            candidate = now;
            confirm = 1;
        }
        if (confirm >= 2 && candidate != stable) {
            stable = candidate;
            ESP_LOGD(TAG, "key mv=%d name=%s", mv,
                     stable == K_UP ? "UP" : stable == K_DOWN ? "DOWN"
                     : stable == K_OK ? "OK" : "NONE");
            portENTER_CRITICAL(&s_mux);
            latch(stable == K_UP, &s_state.up, 0);
            latch(stable == K_DOWN, &s_state.down, 1);
            latch(stable == K_OK, &s_state.ok, 2);
            portEXIT_CRITICAL(&s_mux);
            s_last_change_us = esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void input_init(void)
{
    s_last_change_us = esp_timer_get_time();
    /* External 10 kOhm pull-up; an internal pull collapses the ladder. */
    gpio_pullup_dis(BOARD_KEYS_GPIO);
    gpio_pulldown_dis(BOARD_KEYS_GPIO);
    const adc_oneshot_unit_init_cfg_t unit = { .unit_id = BOARD_KEYS_ADC_UNIT };
    if (adc_oneshot_new_unit(&unit, &s_adc) != ESP_OK) {
        ESP_LOGE(TAG, "adc unit failed");
        return;
    }
    const adc_oneshot_chan_cfg_t chan = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(s_adc, BOARD_KEYS_ADC_CHAN, &chan) != ESP_OK) {
        ESP_LOGE(TAG, "adc channel failed");
        return;
    }
    const adc_cali_curve_fitting_config_t cal = {
        .unit_id = BOARD_KEYS_ADC_UNIT,
        .chan = BOARD_KEYS_ADC_CHAN,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) {
        ESP_LOGE(TAG, "adc calibration failed");
        return;
    }
    xTaskCreate(input_task, "keys", 3072, NULL, 4, NULL);
}

input_state_t input_take(void)
{
    input_state_t out;
    portENTER_CRITICAL(&s_mux);
    out = s_state;
    out.up_pressed = s_pressed[0];
    out.down_pressed = s_pressed[1];
    out.ok_pressed = s_pressed[2];
    out.up_released = s_released[0];
    out.down_released = s_released[1];
    out.ok_released = s_released[2];
    memset(s_pressed, 0, sizeof(s_pressed));
    memset(s_released, 0, sizeof(s_released));
    portEXIT_CRITICAL(&s_mux);
    out.any_pressed = out.up_pressed || out.down_pressed || out.ok_pressed;
    return out;
}

float input_idle_s(void)
{
    return (float)(esp_timer_get_time() - s_last_change_us) / 1e6f;
}
