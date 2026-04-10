#ifndef CONFIG_H
#define CONFIG_H

#include "esp_err.h"

void config_init(void);

void nvs_init(void);
void bt_controller_init_enable(void);
void bluedroid_init_enable(void);

#endif
