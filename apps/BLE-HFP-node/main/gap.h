#ifndef GAP_H
#define GAP_H

#include <stdbool.h>
#include <string.h>
#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"

#ifndef BT_PIN
#define BT_PIN "0000"
#endif

#ifndef REMOTE_DEVICE_NAME
#define REMOTE_DEVICE_NAME ""
#endif

void gap_init(void);
bool gap_is_ring_active(void);
bool gap_is_audio_active(void);
void gap_answer_call(void);
void gap_reject_call(void);

void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
void bt_hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param);

#endif
