#include "boot.h"

#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

static const char *TAG = "boot";

static const esp_partition_t *assistant_partition(void)
{
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    if (!p) {
        return NULL;
    }
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(p, &desc) != ESP_OK) {
        return NULL;
    }
    if (strcmp(desc.project_name, esp_app_get_description()->project_name) == 0) {
        return NULL;   /* another copy of the game, not the assistant */
    }
    return p;
}

void boot_mark_valid(void)
{
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK
        && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

bool boot_assistant_available(void)
{
    return assistant_partition() != NULL;
}

void boot_to_assistant(void)
{
    const esp_partition_t *p = assistant_partition();
    if (!p) {
        ESP_LOGW(TAG, "no assistant firmware in the other app slot");
        return;
    }
    esp_err_t err = esp_ota_set_boot_partition(p);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set boot partition: %s", esp_err_to_name(err));
        return;
    }
    esp_restart();
}
