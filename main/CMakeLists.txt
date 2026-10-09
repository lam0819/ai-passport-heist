idf_component_register(
    SRCS
        "main.c"
        "boot.c"
        "display.c"
        "input.c"
        "gfx.c"
        "assets.c"
        "levels.c"
        "world.c"
        "art.c"
        "sound.c"
        "game.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_spi esp_driver_ledc esp_driver_gpio esp_driver_i2c esp_driver_i2s
             esp_adc esp_timer esp_lcd nvs_flash app_update esp_system esp_app_format
)
