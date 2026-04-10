#include "gap.h"
#include "esp_log.h"
#include "esp_hf_client_api.h"

#define BT_HF_TAG "BT_HF"

void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
}

void bt_hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param)
{
}

void gap_init(void)
{
    esp_bt_gap_set_device_name("ESP_HFP_HF");

    esp_bt_gap_register_callback(bt_gap_cb);
    esp_hf_client_register_callback(bt_hf_client_cb);
    esp_hf_client_init();

#if (CONFIG_EXAMPLE_SSP_ENABLED == true)
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
#endif

    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code;
    const char *pin_str = BT_PIN;
    for (int i = 0; i < 4; i++) pin_code[i] = pin_str[i];
    esp_bt_gap_set_pin(pin_type, 4, pin_code);

    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    ESP_LOGI(BT_HF_TAG, "Starting device discovery...");
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
}
