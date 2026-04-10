#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"

void app_main(void)
{
    config_init();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
