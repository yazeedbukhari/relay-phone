#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bt_init.h"
#include "esp_log.h"
#include "esp_bt_device.h"
#include "bt.h"
#include "ringtone.h"
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
    bt_init_stack();
    buttons_init();
    audio_init();
    bt_gap_init();

    char bda_str[18] = {0};
    const uint8_t *own_bda = esp_bt_dev_get_address();
    const char *own_bda_str = bda2str((uint8_t *)own_bda, bda_str, sizeof(bda_str));
    ESP_LOGI("BT_HF", "Own address:[%s]", own_bda_str ? own_bda_str : "unavailable");

    while (1) {
        buttons_poll();

        if (hfp_is_ring_active() && !hfp_is_audio_active()) {
            ringtone_play_tick();
        } else {
            ringtone_stop();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
