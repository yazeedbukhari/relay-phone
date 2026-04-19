#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"
#include "esp_log.h"
#include "esp_bt_device.h"
#include "gap.h"
#include "phone.h"
#include "buttons.h"
#include "audio.h"

static char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

void app_main(void)
{
    config_init();
    buttons_init();
    audio_init();
    gap_init();

    char bda_str[18] = {0};
    const uint8_t *own_bda = esp_bt_dev_get_address();
    const char *own_bda_str = bda2str((uint8_t *)own_bda, bda_str, sizeof(bda_str));
    ESP_LOGI("BT_HF", "Own address:[%s]", own_bda_str ? own_bda_str : "unavailable");

    while (1) {
        buttons_poll();

        if (gap_is_ring_active() && !gap_is_audio_active()) {
            phone_play_ringtone_tick();
        } else {
            phone_stop_ringtone();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
