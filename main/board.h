// Hardware map for the FoloToy AI Passport (ESP32-C3, 8 MB, no PSRAM).
// Pin source: the board's reference BSP (bsp_pins.h), same as the assistant
// firmware uses. LCD is an ST7789P3, 240x320, driven in landscape here.
#pragma once

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

#define BOARD_LCD_HOST      SPI2_HOST
#define BOARD_LCD_CS        GPIO_NUM_1
#define BOARD_LCD_DC        GPIO_NUM_20
#define BOARD_LCD_SCLK      GPIO_NUM_8
#define BOARD_LCD_MOSI      GPIO_NUM_9
#define BOARD_LCD_BL        GPIO_NUM_21
#define BOARD_LCD_PCLK_HZ   (40 * 1000 * 1000)

#define BOARD_KEYS_GPIO     GPIO_NUM_0
#define BOARD_KEYS_ADC_UNIT ADC_UNIT_1
#define BOARD_KEYS_ADC_CHAN ADC_CHANNEL_0

// The key ladder: UP [0,150) mV, DOWN [150,447), OK [447,1900); idle ~3.3 V.
#define BOARD_KEY_UP_MAX_MV   150
#define BOARD_KEY_DOWN_MAX_MV 447
#define BOARD_KEY_OK_MAX_MV   1900
