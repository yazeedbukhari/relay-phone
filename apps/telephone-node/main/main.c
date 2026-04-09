#include "driver/dac_cosine.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    static const char *TAG = "telephone-node";
    dac_cosine_handle_t dac_handle;

    dac_cosine_config_t cos_cfg = {
        .chan_id = DAC_CHAN_0, // GPIO25
        .freq_hz = 1000,
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .atten = DAC_COSINE_ATTEN_DEFAULT,
        .phase = DAC_COSINE_PHASE_0,
        .offset = 0,
        .flags.force_set_freq = false,
    };

    ESP_ERROR_CHECK(dac_cosine_new_channel(&cos_cfg, &dac_handle));
    ESP_ERROR_CHECK(dac_cosine_start(dac_handle));
    ESP_LOGI(TAG, "Telephone node tone started on GPIO25, %lu Hz", (unsigned long)cos_cfg.freq_hz);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
