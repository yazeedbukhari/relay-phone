#include "phone.h"
#include <stdbool.h>
#include "driver/dac_cosine.h"
#include "esp_err.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

#define PHONE_TAG "PHONE"
#define RING_FREQ_HZ 1000

static bool ringtone_active = false;
#if SOC_DAC_SUPPORTED
static dac_cosine_handle_t ringtone_dac = NULL;
#endif

void phone_play_ringtone_tick(void)
{
    if (!ringtone_active) {
#if SOC_DAC_SUPPORTED
        if (ringtone_dac == NULL) {
            dac_cosine_config_t cosine_cfg = {
                .chan_id = DAC_CHAN_0,
                .freq_hz = RING_FREQ_HZ,
                .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
                .atten = DAC_COSINE_ATTEN_DB_12,
                .phase = DAC_COSINE_PHASE_0,
                .offset = 0,
                .flags.force_set_freq = false,
            };

            esp_err_t err = dac_cosine_new_channel(&cosine_cfg, &ringtone_dac);
            if (err != ESP_OK) {
                ESP_LOGE(PHONE_TAG, "dac_cosine_new_channel failed: %s", esp_err_to_name(err));
                return;
            }
        }

        esp_err_t err = dac_cosine_start(ringtone_dac);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(PHONE_TAG, "dac_cosine_start failed: %s", esp_err_to_name(err));
            return;
        }
#else
        ESP_LOGW(PHONE_TAG, "SOC has no DAC support; ringtone not available");
#endif

        ESP_LOGI(PHONE_TAG, "Ringtone start");
        ringtone_active = true;
    }
}

void phone_stop_ringtone(void)
{
    if (!ringtone_active) {
        return;
    }

#if SOC_DAC_SUPPORTED
    if (ringtone_dac) {
        esp_err_t err = dac_cosine_stop(ringtone_dac);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(PHONE_TAG, "dac_cosine_stop failed: %s", esp_err_to_name(err));
        }
    }
#endif

    ESP_LOGI(PHONE_TAG, "Ringtone stop");
    ringtone_active = false;
}
