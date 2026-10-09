#include "boot.h"
#include "display.h"
#include "esp_check.h"
#include "game.h"
#include "input.h"

void app_main(void)
{
    boot_mark_valid();
    ESP_ERROR_CHECK(display_init());
    input_init();
    game_run();
}
