#ifndef GAP_H
#define GAP_H

#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"

void gap_init(void);

void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
void bt_hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param);

#endif
