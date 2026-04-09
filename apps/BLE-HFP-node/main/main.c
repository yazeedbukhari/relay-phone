#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    static const char *TAG = "relay-tx";

    ESP_LOGI(TAG, "Transmitter firmware running");

    while (1) {
        ESP_LOGI(TAG, "TX heartbeat");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
