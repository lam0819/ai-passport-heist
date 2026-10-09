#include "display.h"

#include <string.h>

#include "board.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "display";

/* ST7789P3 vendor init, from the assistant firmware's board file (same
 * panel). SLPOUT, COLMOD, INVON, DISPON and MADCTL are sent by esp_lcd. */
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t len;
    uint16_t delay_ms;
} st_cmd_t;

static const st_cmd_t k_init[] = {
    { 0xB2, { 0x05, 0x05, 0x00, 0x33, 0x33 }, 5, 0 },
    { 0xB7, { 0x35 }, 1, 0 },
    { 0xBB, { 0x21 }, 1, 0 },
    { 0xC0, { 0x2C }, 1, 0 },
    { 0xC2, { 0x01 }, 1, 0 },
    { 0xC3, { 0x0B }, 1, 0 },
    { 0xC4, { 0x20 }, 1, 0 },
    { 0xC6, { 0x0F }, 1, 0 },
    { 0xD0, { 0xA7, 0xA1 }, 2, 0 },
    { 0xD0, { 0xA4, 0xA1 }, 2, 0 },
    { 0xD6, { 0xA1 }, 1, 0 },
    { 0xE0, { 0xD0, 0x04, 0x08, 0x0A, 0x09, 0x05, 0x2D, 0x43,
              0x49, 0x09, 0x16, 0x15, 0x26, 0x2B }, 14, 0 },
    { 0xE1, { 0xD0, 0x03, 0x09, 0x0A, 0x0A, 0x06, 0x2E, 0x44,
              0x40, 0x3A, 0x15, 0x15, 0x26, 0x2A }, 14, 10 },
};

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e,
                          void *ctx)
{
    (void)io;
    (void)e;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    const ledc_channel_config_t channel = {
        .gpio_num = BOARD_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight channel");
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1023);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    return ESP_OK;
}

esp_err_t display_init(void)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    const spi_bus_config_t bus = {
        .sclk_io_num = BOARD_LCD_SCLK,
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = SCREEN_W * BAND_ROWS * 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_HOST, &bus, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_CS,
        .dc_gpio_num = BOARD_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = BOARD_LCD_PCLK_HZ,
        .trans_queue_depth = 2,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(BOARD_LCD_HOST, &io_cfg, &s_io),
                        TAG, "panel io");

    s_done = xSemaphoreCreateBinary();
    xSemaphoreGive(s_done);
    const esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = on_color_done };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, NULL),
                        TAG, "panel io callbacks");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,   /* RST tied high; reset() sends SWRESET */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,   /* pixels arrive byte-swapped */
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    for (size_t i = 0; i < sizeof(k_init) / sizeof(k_init[0]); i++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, k_init[i].cmd, k_init[i].data,
                                                     k_init[i].len),
                            TAG, "vendor cmd 0x%02x", k_init[i].cmd);
        if (k_init[i].delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(k_init[i].delay_ms));
        }
    }
    // Landscape; this is the orientation the test build verified on hardware.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "swap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 0, 0), TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "on");

    ESP_LOGI(TAG, "panel up; free heap %u", (unsigned)esp_get_free_heap_size());
    return ESP_OK;
}

void display_send_band(int y, const uint16_t *buf, int rows)
{
    xSemaphoreTake(s_done, portMAX_DELAY);
    esp_lcd_panel_draw_bitmap(s_panel, 0, y, SCREEN_W, y + rows, buf);
}

void display_set_backlight(int pct)
{
    if (pct < 0) {
        pct = 0;
    } else if (pct > 100) {
        pct = 100;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)(pct * 1023 / 100));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void display_power(bool on)
{
    /* The last band must be out before the panel goes to sleep. */
    xSemaphoreTake(s_done, portMAX_DELAY);
    xSemaphoreGive(s_done);
    display_set_backlight(on ? 100 : 0);
    esp_lcd_panel_disp_on_off(s_panel, on);
    esp_lcd_panel_disp_sleep(s_panel, !on);
}
